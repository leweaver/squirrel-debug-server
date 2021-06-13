/*---------------------------------------------------------
 * Copyright (C) Microsoft Corporation. All rights reserved.
 *--------------------------------------------------------*/

import { logger} from 'vscode-debugadapter';
import { EventEmitter } from 'events';

//import encodeUrl = require('encodeurl');
//import got = require('got');
import WebSocket = require('ws');

export interface FileAccessor {
    readFile(path: string): Promise<string>;
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


    constructor(private _fileAccessor: FileAccessor) {
        super();
    }

    /**
     * Start executing the given program.
     */
    public async start(hostnamePort: string, stopOnEntry: boolean, noDebug: boolean): Promise<void> {

        this._noDebug = noDebug;

        this._debuggerHostnamePort = hostnamePort;
        await this.connectDebugger(this._debuggerHostnamePort).catch(_ => this.emit('end'));
        logger.log('connected');

        //await this.loadSource(program);
        //this._currentLine = -1;

        //await this.verifyBreakpoints(this._sourceFile);

        if (stopOnEntry) {
            // we step once
            this.step('stopOnEntry');
        } else {
            // we just start to run until we hit a breakpoint or an exception
            this.continue();
        }
    }

    /**
     * Continue execution to the end/beginning.
     */
    public continue() {
        this.run(undefined);
    }

    /**
     * Step to the next/previous non empty line.
     */
    public step(event = 'stopOnStep') {
        this.run(event);
    }

    /**
     * Returns a fake 'stacktrace' where every 'stackframe' is a word from the current line.
     */
    public stack(startFrame: number, endFrame: number): IStack {
        return {
            frames: [{
                index: 0,
                name: `helloWorld()`,
                file: "hello world",
                line: 1
            }],
            count: 1
        };
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
        var wsMessageHandler = this.onWsMessage;
        return new Promise<void>((resolve, reject) => {
            const ws = new WebSocket(`ws://${hostnamePort}/ws`);
            ws.on('open', function open() {
                ws.send("send_status");
                resolve();
            });
            ws.on('message', wsMessageHandler);
            ws.on('error', (evt: WebSocket.ErrorEvent) => {
                logger.error(evt.message);
                reject("Failed to connect: " + evt.message);
            });
        });
    }

    private onWsMessage(msg: string): void {
        logger.log("Received: " + msg);
    }

    private async loadSource(file: string): Promise<string[]> {
        let sourceLines = this._sourceLines[file];

        if (typeof(sourceLines) == "undefined") {
            const contents = await this._fileAccessor.readFile(file);
            this._sourceLines[file] = contents.split(/\r?\n/);
        }
        return this._sourceLines[file];
    }

    /**
     * Run through the file.
     * If stepEvent is specified only run a single step and emit the stepEvent.
     */
    private run(stepEvent?: string) {
    }

    private sendEvent(event: string, ... args: any[]) {
        setImmediate(_ => {
            this.emit(event, ...args);
        });
    }
}