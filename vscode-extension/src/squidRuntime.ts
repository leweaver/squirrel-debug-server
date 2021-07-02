/*---------------------------------------------------------
 * Copyright (C) Microsoft Corporation. All rights reserved.
 *--------------------------------------------------------*/

import { logger} from 'vscode-debugadapter';
import { EventEmitter } from 'events';
import { EventMessage, EventMessageType, Status, OutputLine, Runstate, Variable, ResolvedBreakpoint } from './squidDto';

import encodeUrl = require('encodeurl');
import got = require('got');
import WebSocket = require('ws');

import cp = require("child_process");
import terminate = require("terminate");

import { window } from "vscode";

export interface FileAccessor {
    readFile(path: string): Promise<string>;
}

export interface ISquidCreateBreakpoint {
    line: number;
}

export interface ISquidBreakpoint extends ISquidCreateBreakpoint {
    id: number;
    line: number;
    verified: boolean;
}

interface IStackFrame {
    index: number;
    name: string;
    file: string;
    line: number;
    column?: number;
}

interface IStack {
    count: number;
    frames: IStackFrame[];
}

/**
 * A Squid runtime with minimal debugger functionality.
 */
export class SquidRuntime extends EventEmitter {

    // the initial (and one and only) file we are 'debugging'
    //private _sourceFile: string = '';
    //public get sourceFile() {
        //return this._sourceFile;
    //}

    // Files that we've loaded lines for
    private _sourceLines = new Map<string, string[]>();

    // This is the next line that will be 'executed'
    //private _currentLine = 0;
    //private _currentColumn: number | undefined;

    // maps from sourceFile to array of Squid breakpoints
    private _breakPoints = new Map<string, ISquidBreakpoint[]>();

    // since we want to send breakpoint events, we will assign an id to every event
    // so that the frontend can match events with breakpoints.
    private _breakpointId = 1;

    //private _breakAddresses = new Set<string>();

    private _connected = false;

    //private _namedException: string | undefined;
    //private _otherExceptions = false;

    private _debuggerHostnamePort = "localhost:8000";
    private _ws?: WebSocket = undefined;
    private _pid: number = 0;

    private _status?: Status = undefined;


    constructor(private _fileAccessor: FileAccessor) {
        super();
        logger.log("Creating new squidRuntime");
    }

    /**
     * Start executing the given program.
     */
    public async start(hostnamePort: string, program: string, noDebug: boolean): Promise<void> {

        logger.log('in start hostnamePort=' + hostnamePort + ", program=" + program);
        // TODO: enable noDebug on server side?
        if (program) {
			let executableLine = `${program}`;
            // todo: add breakpoints on command line? how do we stop runaway?
            const exec = cp.exec(executableLine);
            exec.on('error', (error) => {
				window.showErrorMessage(`Failed to launch instance: ${error}`);
                this.emit('end');
			});

            const pid = exec.pid;
            this._pid = pid;
            logger.log('launched with PID ' + exec.pid);

            exec.on("exit", (code) => {
                logger.log(`process ${pid} exited with code ${code}`);
                this.emit('end');
            });
        }

        this._debuggerHostnamePort = hostnamePort;
        const self = this;
        await this.connectDebugger(this._debuggerHostnamePort)
            .then(async () => {
                logger.log('in start callback: connected=' + self._connected + " _breakPoints.length=" + (self._breakPoints?.size ?? "null"));
                for (const file of self._breakPoints.keys()) {
                    logger.log('async connected: ' + self._connected);
                    await self.verifyBreakpoints(file);
                };
                logger.log('sending SendStatus command');
                this.sendCommand('SendStatus');
            })
            .catch(e => {
                if (e instanceof Error) {
                    e = e.message;
                }
                logger.error(e);
                this.emit('end');
            });
    }

    public async stop(): Promise<void> {
        if (this._pid) {
            terminate.default(this._pid);
            this._pid = 0;
        }
        if (this._ws) {
            this._ws?.close();
            this._ws = undefined;
        }
    }

    /**
     * Continue execution to the end/beginning.
     */
    public async continue() {
        await this.sendCommand('Continue');
    }

    /**
     * Step to the next/previous non empty line.
     */
    public async stepOut() {
        await this.sendCommand('StepOut');
    }
    public async stepOver() {
        await this.sendCommand('StepOver');
    }
    public async stepIn() {
        await this.sendCommand('StepIn');
    }

    /**
     * Returns a fake 'stacktrace' where every 'stackframe' is a word from the current line.
     */
    public stack(startFrame: number, endFrame: number): IStack {
        let frames: IStackFrame[];
        if (this._status !== undefined && this._status?.runstate === Runstate.paused) {
            frames = [];
            let statusStack = this._status?.stack;
            for (let i = 0; i < statusStack.length; i++) {
                let statusStackEntry = statusStack[i];
                frames.push({
                    index: i,
                    name: statusStackEntry.function,
                    file: statusStackEntry.file,
                    line: statusStackEntry.line
                });
            }
        } else {
            frames = [];
        }
        return {
            frames: frames,
            count: frames.length
        };
    }

    public async getLocalVariables(stackFrame: number, path:string): Promise<Variable[]> {
        const dto = await this.sendQuery('Variables/Local/' + stackFrame + '?path=' + encodeUrl(path));
        return dto.variables.map((instanceData: any) => new Variable(instanceData));
    }

    public async getGlobalVariables(path:string): Promise<Variable[]> {
        const dto = await this.sendQuery('Variables/Global?path=' + encodeUrl(path));
        return dto.variables.map((instanceData: any) => new Variable(instanceData));
    }

    /*
     * Set breakpoint in file with given line.
     */
    public async setBreakPoint(path: string, line: number): Promise<ISquidBreakpoint> {

        const bp: ISquidBreakpoint = { verified: false, line, id: this._breakpointId++ };
        let bps = this._breakPoints.get(path);
        if (!bps) {
            bps = new Array<ISquidBreakpoint>();
            this._breakPoints.set(path, bps);
        }
        bps.push(bp);

        await this.verifyBreakpoints(path);

        return bp;
    }

    /*
     * Clear breakpoint in file with given line.
     */
    public clearBreakPoint(path: string, line: number): ISquidBreakpoint | undefined {
        const bps = this._breakPoints.get(path);
        if (bps) {
            const index = bps.findIndex(bp => bp.line === line);
            if (index >= 0) {
                const bp = bps[index];
                bps.splice(index, 1);
                return bp;
            }
        }
        return undefined;
    }

    private async verifyBreakpoints(path: string): Promise<void> {
        const bps = this._breakPoints.get(path);
        logger.log("in verifyBreakpoints bps.length=" + (bps?.length ?? "null") + " _connected=" + this._connected);
        if (bps) {
            if (this._connected) {
                const breakpoints = bps.map((bp: ISquidBreakpoint) => {
                    return {
                        "id": bp.id,
                        "line": bp.line
                    };
                });

                const remoteBps = await this.sendCommand('FileBreakpoints', {
                    file: path, 
                    breakpoints: breakpoints,
                });
                
                // Replace the existing BP records with the resovled ones.
                const resolvedBpDtos = remoteBps.breakpoints.map((instanceData: any) => new ResolvedBreakpoint(instanceData));
                this._breakPoints.set(path, resolvedBpDtos);
                
                resolvedBpDtos.forEach((bp: ResolvedBreakpoint) => {
                    this.sendEvent('breakpointValidated', bp);
                });
            } else {
                bps.forEach(bp => {
                    if (bp.verified = false) {
                        this.sendEvent('breakpointValidated', bp);
                    }
                });
            }
        } else {
            logger.log('no bps for path: ' + path);
        }
    }

    /**
     * Clears, then recreates all breakpoints for the given file. Will validate all breakpoints.
     */
    public async setFileBreakpoints(path: string, newBreakpoints: ISquidCreateBreakpoint[]): Promise<ISquidBreakpoint[]> {
        logger.log("in setFileBreakpoints");
        this._breakPoints.delete(path);
        let bps = new Array<ResolvedBreakpoint>();
        this._breakPoints.set(path, bps);
        
        for (const newBp of newBreakpoints) {
            const bp: ResolvedBreakpoint = Object.assign({}, newBp, {id: this._breakpointId++, verified: false});
            bps.push(bp);
        }
        
        await this.verifyBreakpoints(path);
        return bps;
    }

    // private methods

    private async connectDebugger(hostnamePort: string): Promise<void> {  
        logger.log('in connectDebugger');      
        if (typeof(hostnamePort) === undefined) {
            throw new Error("hostname and port must be provided.");
        }
        
        let self = this;

        // Try to reach the debugger by GET request first
        await got(`http://${hostnamePort}/`, {
                retry: {
                    retries: 5
                }
            });

        // GET request to index was successful, so debug server has started. Now try to establish websocket.
        return new Promise<void>((resolve, reject) => {
            let ws = new WebSocket(`ws://${hostnamePort}/ws`);
            self._ws = ws;
            ws.on('open', function open() {
                logger.log('open');
                self._connected = true;
                resolve();
            });
            ws.on('message', (msgStr: string) => self.handleWebsocketMessage(msgStr));
            ws.on('error', (evt: WebSocket.ErrorEvent) => {
                logger.error(evt.message);
                reject("Failed to connect: " + evt.message);
            });
            ws.on('close', (code: number, reason: string) => {
                logger.log(`Websocket connection closed (${code}: ${reason}`);
                self._connected = false;
                this.emit('end');
            });
        });
    }

    private handleWebsocketMessage(msgStr: string): Promise<void> {
        let message: EventMessage;
        try {
            message = new EventMessage(JSON.parse(msgStr));
        } catch (e) {
            logger.error('Failed to parse JSON: ' + msgStr);
            return Promise.reject('Failed to parse JSON');
        }

        if (message.message === undefined) {
            logger.error('Invalid message body: ' + msgStr);
            return Promise.reject('Invalid message body');
        }
        
        try {
            switch (message.type) {
                case EventMessageType.status:
                    this.updateStatus(new Status(message.message));
                    break;
                case EventMessageType.output_line:
                    this.outputLine(new OutputLine(message.message));
                    break;
                default:
                    logger.log("Unabled to handle message: " + message.type);
                    return Promise.reject();
            }
        } catch (e) {
            logger.error('Failed to handle message: ' + msgStr + " (" + e.message + ")");
            return Promise.reject('Failed to handle message');
        }

        logger.log("Handled message: " + message.type);
        return Promise.resolve();
    }

    private async sendCommand(commandName: string, data: any = undefined) {
        let uri = `http://${this._debuggerHostnamePort}/DebugCommand/${commandName}`;
        logger.log(uri);
        const {body} = await got.put(uri, {
            json: true,
            body: data
        });
        return body;
    }
    private async sendQuery(commandName: string) {
        let uri = `http://${this._debuggerHostnamePort}/DebugCommand/${commandName}`;
        logger.log(uri);
        const {body} = await got(uri, {
            json: true
        });
        return body;
    }

    private updateStatus(status: Status) {
        this._status = status;
        
        if (status.runstate === Runstate.paused) {
            if (status.pausedAtBreakpointId > 0) {
                logger.log('Hit breakpoint ' + status.pausedAtBreakpointId);
                this.sendEvent('stopOnBreakpoint');
            } else {
                logger.log('Paused');
                this.sendEvent('stopOnStep');
            }
        } else {
            logger.log('Playing');
            this.sendEvent('continued');
        }
    }

    private outputLine(outputLine: OutputLine) {
        this.sendEvent('output', outputLine.output, outputLine.isErr ? 'stderr' : 'console', outputLine.file, outputLine.line);
    }

    public async loadSource(file: string): Promise<string[]> {
        let sourceLines = this._sourceLines[file];

        if (typeof(sourceLines) === "undefined") {
            const contents = await this._fileAccessor.readFile(file);
            this._sourceLines[file] = contents.split(/\r?\n/);
        }
        return this._sourceLines[file];
    }

    private sendEvent(event: string, ... args: any[]) {
        setImmediate(_ => {
            this.emit(event, ...args);
        });
    }
}