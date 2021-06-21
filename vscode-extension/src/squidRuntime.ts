/*---------------------------------------------------------
 * Copyright (C) Microsoft Corporation. All rights reserved.
 *--------------------------------------------------------*/

import { logger} from 'vscode-debugadapter';
import { EventEmitter } from 'events';
import { EventMessage, EventMessageType, Status, Runstate } from './squidDto';

import encodeUrl = require('encodeurl');
import got = require('got');
import WebSocket = require('ws');

export interface FileAccessor {
    readFile(path: string): Promise<string>;
}

export interface Variable {
    name: string,
    value: string;
    type: string
}

export interface ISquidBreakpoint {
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

    private _noDebug = false;

    //private _namedException: string | undefined;
    //private _otherExceptions = false;

    private _debuggerHostnamePort = "localhost:8000";

    private _status?: Status = undefined;


    constructor(private _fileAccessor: FileAccessor) {
        super();
    }

    /**
     * Start executing the given program.
     */
    public async start(hostnamePort: string, stopOnEntry: boolean, noDebug: boolean): Promise<void> {

        this._noDebug = noDebug;

        this._debuggerHostnamePort = hostnamePort;
        await this.connectDebugger(this._debuggerHostnamePort)
            .then(() => {
                logger.log('connected');
                this.sendCommand('SendStatus');
            })
            .catch(_ => this.emit('end'));

        //await this.loadSource(program);
        //this._currentLine = -1;

        //await this.verifyBreakpoints(this._sourceFile);
/*
        if (stopOnEntry) {
            // we step once
            this.step();
        } else {
            // we just start to run until we hit a breakpoint or an exception
            this.continue();
        }
        */
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
                    line: statusStackEntry.line - 1
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

    public async getStackLocals(frame: number, path:string): Promise<Variable[]> {
        const dto = await this.sendQuery('StackLocals/' + frame + '?path=' + encodeUrl(path));
        return dto.variables;
    }

    public getBreakpoints(path: string, line: number): number[] {

        const l = this._sourceLines[line];

        let sawSpace = true;
        const bps: number[] = [];
        for (let i = 0; i < l.length; i++) {
            if (l[i] !== ' ') {
                if (sawSpace) {
                    bps.push(i);
                    sawSpace = false;
                }
            } else {
                sawSpace = true;
            }
        }

        return bps;
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
        if (this._noDebug) {
            return;
        }

        const bps = this._breakPoints.get(path);
        if (bps) {
            let sourceLines = await this.loadSource(path);
            bps.forEach(bp => {
                if (bp.line >= sourceLines.length) {
                    return;
                }

                if (!bp.verified) {
                    bp.verified = true;
                    this.sendEvent('breakpointValidated', bp);
                }
            });
        }
    }

    /*
     * Clear all breakpoints for file.
     */
    public clearBreakpoints(path: string): void {
        this._breakPoints.delete(path);
    }

    // private methods

    private async connectDebugger(hostnamePort: string): Promise<void> {
        
        logger.log('connectDebugger');
        let self = this;
        return new Promise<void>((resolve, reject) => {
            let ws = new WebSocket(`ws://${hostnamePort}/ws`);
            ws.on('open', function open() {
                resolve();
            });
            ws.on('message', (msgStr: string) => self.handleWebsocketMessage(msgStr));
            ws.on('error', (evt: WebSocket.ErrorEvent) => {
                logger.error(evt.message);
                reject("Failed to connect: " + evt.message);
            });
            ws.on('close', (code: number, reason: string) => {
                logger.log(`Websocket connection closed (${code}: ${reason}`);
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

    private async sendCommand(commandName: string) {
        let uri = `http://${this._debuggerHostnamePort}/DebugCommand/${commandName}`;
        logger.log(uri);
        const {body} = await got.put(uri, {
            json: true
        });
        return body.data;
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
            logger.log('Stopping');
            this.sendEvent('stopOnStep');
        } else {
            logger.log('Playing');
            this.sendEvent('continued');
        }
    }

    private async loadSource(file: string): Promise<string[]> {
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