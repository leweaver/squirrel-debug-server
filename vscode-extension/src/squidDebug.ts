/*---------------------------------------------------------
 * Copyright (C) Microsoft Corporation. All rights reserved.
 *--------------------------------------------------------*/

import {
    Logger, logger,
    DebugSession,
    InitializedEvent, TerminatedEvent, StoppedEvent, BreakpointEvent, OutputEvent,
    ProgressStartEvent, ProgressUpdateEvent, ProgressEndEvent, InvalidatedEvent,
    Thread, StackFrame, Scope, Source, Handles, Breakpoint, ContinuedEvent
} from 'vscode-debugadapter';
import { DebugProtocol } from 'vscode-debugprotocol';
import { basename } from 'path';
import { SquidRuntime, ISquidBreakpoint, FileAccessor } from './squidRuntime';
import { Variable, VariableType } from './squidDto';
import { Subject } from 'await-notify';

function timeout(ms: number) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

/**
 * This interface describes the squid-debug specific launch attributes
 * (which are not part of the Debug Adapter Protocol).
 * The schema for these attributes lives in the package.json of the squid-debug extension.
 * The interface should always match this schema.
 */
interface ILaunchRequestArguments extends DebugProtocol.LaunchRequestArguments {
    /** network host and port that we connect to */
    hostnamePort: string;
    /** application to launch */
    program: string;
    /** enable logging the Debug Adapter Protocol */
    trace?: boolean;
    /** run without debugging */
    noDebug?: boolean;
}

class DeferredBreakpoint extends Breakpoint {
    public id: number;
    constructor(id: number, line: number, verified: boolean) {
        super(verified, line);
        this.id = id;
    }
}

export class SquidDebugSession extends DebugSession {

    // we don't support multiple threads, so we can use a hardcoded ID for the default thread
    private static threadID = 1;

    private _runtime: SquidRuntime;

    private _variableHandles = new Handles<string>();

    private _configurationDone = new Subject();

    private _cancelationTokens = new Map<number, boolean>();

    private _reportProgress = false;
    private _progressId = 10000;
    private _cancelledProgressId: string | undefined = undefined;
    private _isProgressCancellable = true;

    private _showHex = false;
    private _useInvalidatedEvent = false;
    private _useVariableType = false;
    private _useMemoryReferences = false;

    /**
     * Creates a new debug adapter that is used for one debug session.
     * We configure the default implementation of a debug adapter here.
     */
    public constructor(fileAccessor: FileAccessor) {
        super(false, false);

        // this debugger uses one-based lines
        this.setDebuggerLinesStartAt1(true);
        this.setDebuggerColumnsStartAt1(true);

        this._runtime = new SquidRuntime(fileAccessor);

        // setup event handlers
        this._runtime.on('stopOnEntry', () => {
            this.sendEvent(new StoppedEvent('entry', SquidDebugSession.threadID));
        });
        this._runtime.on('stopOnStep', () => {
            this.sendEvent(new StoppedEvent('step', SquidDebugSession.threadID));
        });
        this._runtime.on('stopOnBreakpoint', () => {
            this.sendEvent(new StoppedEvent('breakpoint', SquidDebugSession.threadID));
        });
        this._runtime.on('stopOnDataBreakpoint', () => {
            this.sendEvent(new StoppedEvent('data breakpoint', SquidDebugSession.threadID));
        });
        this._runtime.on('stopOnException', (exception) => {
            if (exception) {
                this.sendEvent(new StoppedEvent(`exception(${exception})`, SquidDebugSession.threadID));
            } else {
                this.sendEvent(new StoppedEvent('exception', SquidDebugSession.threadID));
            }
        });
        this._runtime.on('continued', () => {
            this.sendEvent(new ContinuedEvent(SquidDebugSession.threadID));
        });
        this._runtime.on('breakpointValidated', (bp: ISquidBreakpoint) => {
            logger.log("BP Changed: " + bp.id + " -> " + bp.verified);
            this.sendEvent(new BreakpointEvent('changed', { verified: bp.verified, id: bp.id } as DebugProtocol.Breakpoint));
        });
        this._runtime.on('output', (text, category, file, line) => {
            const e: DebugProtocol.OutputEvent = new OutputEvent(`${text}`, category);
            e.body.source = {path: file};
            e.body.line = this.convertDebuggerLineToClient(line);
            this.sendEvent(e);
        });
        this._runtime.on('end', () => {
            this.sendEvent(new TerminatedEvent());
        });
    }

    /**
     * The 'initialize' request is the first request called by the frontend
     * to interrogate the features the debug adapter provides.
     */
    protected initializeRequest(response: DebugProtocol.InitializeResponse, args: DebugProtocol.InitializeRequestArguments): void {
        this._reportProgress = !!args.supportsProgressReporting;
        this._useVariableType = !!args.supportsVariableType;
        this._useInvalidatedEvent = !!args.supportsInvalidatedEvent;
        this._useMemoryReferences = !!args.supportsMemoryReferences;

        // build and return the capabilities of this debug adapter:
        response.body = response.body || {};

        // the adapter implements the configurationDoneRequest.
        response.body.supportsConfigurationDoneRequest = true;

        // make VS Code use 'evaluate' when hovering over source
        response.body.supportsEvaluateForHovers = true;

        // make VS Code show a 'step back' button
        response.body.supportsStepBack = false;

        // make VS Code support data breakpoints
        response.body.supportsDataBreakpoints = false;

        response.body.supportsLogPoints = true;

        // make VS Code support completion in REPL
        response.body.supportsCompletionsRequest = true;
        response.body.completionTriggerCharacters = [ ".", "[" ];

        // make VS Code send cancelRequests
        response.body.supportsCancelRequest = true;

        // make VS Code send the breakpointLocations request
        response.body.supportsBreakpointLocationsRequest = false;

        // make VS Code provide "Step in Target" functionality
        response.body.supportsStepInTargetsRequest = true;

        // the adapter defines two exceptions filters, one with support for conditions.
        response.body.supportsExceptionFilterOptions = false;
        response.body.exceptionBreakpointFilters = [
            {
                filter: 'namedException',
                label: "Named Exception",
                description: `Break on named exceptions. Enter the exception's name as the Condition.`,
                default: false,
                supportsCondition: true,
                conditionDescription: `Enter the exception's name`
            },
            {
                filter: 'otherExceptions',
                label: "Other Exceptions",
                description: 'This is a other exception',
                default: true,
                supportsCondition: false
            }
        ];

        // make VS Code send exceptionInfoRequests
        response.body.supportsExceptionInfoRequest = true;

        this.sendResponse(response);

        // since this debug adapter can accept configuration requests like 'setBreakpoint' at any time,
        // we request them early by sending an 'initializeRequest' to the frontend.
        // The frontend will end the configuration sequence by calling 'configurationDone' request.
        this.sendEvent(new InitializedEvent());
    }

    /**
     * Called at the end of the configuration sequence.
     * Indicates that all breakpoints etc. have been sent to the DA and that the 'launch' can start.
     */
    protected configurationDoneRequest(response: DebugProtocol.ConfigurationDoneResponse, args: DebugProtocol.ConfigurationDoneArguments): void {
        super.configurationDoneRequest(response, args);

        // notify the launchRequest that configuration has finished
        this._configurationDone.notify();
    }

    protected async launchRequest(response: DebugProtocol.LaunchResponse, args: ILaunchRequestArguments) {

        // TODO:
        // make sure to 'Stop' the buffered logging if 'trace' is not set
        //logger.setup(args.trace === false ? Logger.LogLevel.Stop : Logger.LogLevel.Verbose, false);
        logger.setup(Logger.LogLevel.Verbose, undefined/*'c:\\Squid.txt'*/);

        // wait until configuration has finished (and configurationDoneRequest has been called)
        await this._configurationDone.wait(1000);

        // make the connection in the runtime
        await this._runtime.start(args.hostnamePort, args.program, args.noDebug ?? false);

        this.sendResponse(response);
    }

    protected async setBreakPointsRequest(response: DebugProtocol.SetBreakpointsResponse, args: DebugProtocol.SetBreakpointsArguments): Promise<void> {

        const path = args.source.path as string;
        const bps = (args.breakpoints || []).map(bp => ({line: bp.line}));
        
        logger.verbose('setBreakPointsRequest: ' + path);

        // set and verify breakpoint locations
        const actualBreakpoints =  (await this._runtime.setFileBreakpoints(path, bps)).map(bp => new DeferredBreakpoint(bp.id, this.convertDebuggerLineToClient(bp.line), bp.verified));

        // send back the actual breakpoint positions
        response.body = {
            breakpoints: actualBreakpoints
        };
        this.sendResponse(response);
    }

    protected async setExceptionBreakPointsRequest(response: DebugProtocol.SetExceptionBreakpointsResponse, args: DebugProtocol.SetExceptionBreakpointsArguments): Promise<void> {

        let namedException: string | undefined = undefined;
        let otherExceptions = false;

        if (args.filterOptions) {
            for (const filterOption of args.filterOptions) {
                switch (filterOption.filterId) {
                    case 'namedException':
                        namedException = args.filterOptions[0].condition;
                        break;
                    case 'otherExceptions':
                        otherExceptions = true;
                        break;
                }
            }
        }

        if (args.filters) {
            if (args.filters.indexOf('otherExceptions') >= 0) {
                otherExceptions = true;
            }
        }

        // TODO:
        //this._runtime.setExceptionsFilters(namedException, otherExceptions);
        logger.log("UNIMPLEMENTED: " + namedException + otherExceptions);

        this.sendResponse(response);
    }

    protected exceptionInfoRequest(response: DebugProtocol.ExceptionInfoResponse, args: DebugProtocol.ExceptionInfoArguments) {
        response.body = {
            exceptionId: 'Exception ID',
            description: 'This is a descriptive description of the exception.',
            breakMode: 'always',
            details: {
                message: 'Message contained in the exception.',
                typeName: 'Short type name of the exception object',
                stackTrace: 'stack frame 1\nstack frame 2',
            }
        };
        this.sendResponse(response);
    }

    protected threadsRequest(response: DebugProtocol.ThreadsResponse): void {

        // runtime supports no threads so just return a default thread.
        response.body = {
            threads: [
                new Thread(SquidDebugSession.threadID, "thread 1")
            ]
        };
        this.sendResponse(response);
    }

    protected stackTraceRequest(response: DebugProtocol.StackTraceResponse, args: DebugProtocol.StackTraceArguments): void {

        const startFrame = typeof args.startFrame === 'number' ? args.startFrame : 0;
        const maxLevels = typeof args.levels === 'number' ? args.levels : 1000;
        const endFrame = startFrame + maxLevels;

        const stk = this._runtime.stack(startFrame, endFrame);

        response.body = {
            stackFrames: stk.frames.map(f => {
                const sf = new StackFrame(f.index, f.name, this.createSource(f.file), this.convertDebuggerLineToClient(f.line));
                if (typeof f.column === 'number') {
                    sf.column = this.convertDebuggerColumnToClient(f.column);
                }
                return sf;
            }),
            //no totalFrames: 				// VS Code has to probe/guess. Should result in a max. of two requests
            totalFrames: stk.count			// stk.count is the correct size, should result in a max. of two requests
            //totalFrames: 1000000 			// not the correct size, should result in a max. of two requests
            //totalFrames: endFrame + 20 	// dynamically increases the size with every requested chunk, results in paging
        };
        this.sendResponse(response);
    }

    protected scopesRequest(response: DebugProtocol.ScopesResponse, args: DebugProtocol.ScopesArguments): void {
        response.body = {
            scopes: [
                new Scope("Local", this._variableHandles.create("local:" + args.frameId + ':'), false),
                new Scope("Global", this._variableHandles.create("global:"), true)
            ]
        };
        this.sendResponse(response);
    }

    protected async variablesRequest(response: DebugProtocol.VariablesResponse, args: DebugProtocol.VariablesArguments, request?: DebugProtocol.Request) {
        const id = this._variableHandles.get(args.variablesReference);

        let getVariablesPms: Promise<Variable[]>;
        if (id.startsWith('local:')) {
            const secondPos = id.indexOf(':', 6);
            const frameId = id.substr(6, secondPos - 6);
            const path:string = id.substr(secondPos + 1);
            logger.log(`variablesRequest id=${id} frameId=${frameId} path=${path}`);
            getVariablesPms = this._runtime.getLocalVariables(parseInt(frameId), path);
        } else if (id.startsWith('global:')) {
            const path = id.substr(7);
            getVariablesPms = this._runtime.getGlobalVariables(path);
        } else {
            getVariablesPms = Promise.reject('Unknown variable scope: ' + id);
        }
        
        getVariablesPms.then((vars) => {
            const variables: DebugProtocol.Variable[] = [];
            for (let v of vars) {
                let debugVarInfo = {
                    name: v.pathUiString,
                    value: v.value,
                    variablesReference: 0,
                    presentationHint: undefined,
                } as DebugProtocol.Variable;
                
                // Fields that clients may not support
                if (this._useVariableType) {
                    debugVarInfo.type = VariableType[v.valueType];
                }
                if (this._useMemoryReferences && v.valueRawAddress) {
                    debugVarInfo.memoryReference = '0x' + v.valueRawAddress.toString(16);
                }

                // Annotations and style
                if (v.valueType === VariableType.integer) {
                    const value: number = parseInt(v.value);
                    if (typeof(value) === 'number' && !isNaN(value) && this._showHex) {
                        debugVarInfo.value = '0x' + value.toString(16);
                    }
                }
                if (v.valueType === VariableType.closure) {
                    debugVarInfo.presentationHint = {
                        kind: 'method'
                    };
                    debugVarInfo.value = 'FUNCTION ' + v.value;
                } else if (v.valueType === VariableType.class) {
                    debugVarInfo.presentationHint = {
                        kind: 'class'
                    };
                    debugVarInfo.value = 'CLASS ' + v.value;
                } else if (v.valueType === VariableType.instance) {
                    debugVarInfo.value = `<${v.instanceClassName ?? 'INSTANCE'}> ${v.value}`; 
                    if (v.valueRawAddress) {
                        v.value += ` (0x${v.valueRawAddress})`;
                    }
                } else if (v.valueType === VariableType.table || v.valueType === VariableType.array) {
                    debugVarInfo.value = `${VariableType[v.valueType].toUpperCase()} v.value`;
                }

                // Child id's
                if (v.childCount > 0) {
                    let subobjectId = id;
                    if (!id.endsWith(':')) { subobjectId += ','; }
                    subobjectId += v.pathIterator;

                    debugVarInfo.variablesReference = this._variableHandles.create(subobjectId);
                }

                variables.push(debugVarInfo);
            }

            response.body = {
                variables: variables
            };
            this.sendResponse(response);
        }).catch((reason: any) => {
            if (reason instanceof Error) {
                reason = reason.name + ": " + reason.message;
            }

            logger.error(`variablesRequest failed. reason: ${reason}`);
            response.success = false;
            response.message = "Failed to retrieve variables.";
            this.sendResponse(response);
        });
    }

    protected continueRequest(response: DebugProtocol.ContinueResponse, args: DebugProtocol.ContinueArguments): void {
        this._runtime.continue().then(() => this.sendResponse(response));
    }

    protected nextRequest(response: DebugProtocol.NextResponse, args: DebugProtocol.NextArguments): void {
        this._runtime.stepOver().then(() => this.sendResponse(response));
    }

    protected stepInTargetsRequest(response: DebugProtocol.StepInTargetsResponse, args: DebugProtocol.StepInTargetsArguments) {
        
        // TODO:
        /*const targets = this._runtime.getStepInTargets(args.frameId);
        response.body = {
            targets: targets.map(t => {
                return { id: t.id, label: t.label };
            })
        };
        this.sendResponse(response);
        */
    }

    protected stepInRequest(response: DebugProtocol.StepInResponse, args: DebugProtocol.StepInArguments): void {
        this._runtime.stepIn().then(() => this.sendResponse(response));
    }

    protected stepOutRequest(response: DebugProtocol.StepOutResponse, args: DebugProtocol.StepOutArguments): void {
        this._runtime.stepOut().then(() => this.sendResponse(response));
    }
    
    protected terminateRequest(response: DebugProtocol.TerminateResponse, args: DebugProtocol.TerminateArguments, request?: DebugProtocol.Request): void {
        this._runtime.stop().then(() => this.sendResponse(response));
    }
    protected async evaluateRequest(response: DebugProtocol.EvaluateResponse, args: DebugProtocol.EvaluateArguments): Promise<void> {

        let reply: string | undefined = undefined;

        if (args.context === 'repl') {

            if (1 === 1) {
                throw new Error("Not sure what all this mumbo jumbo is, and it certainly won't work with the remote debugger.");
            }

            // 'evaluate' supports to create and delete breakpoints from the 'repl':
            const matches = /new +([0-9]+)/.exec(args.expression);
            
            // TODO:
            var sourceFile ="";
            if (matches && matches.length === 2) {
                const mbp = await this._runtime.setBreakPoint(sourceFile, this.convertClientLineToDebugger(parseInt(matches[1])));
                const bp = new Breakpoint(mbp.verified, this.convertDebuggerLineToClient(mbp.line), undefined, this.createSource(sourceFile)) as DebugProtocol.Breakpoint;
                bp.id= mbp.id;
                this.sendEvent(new BreakpointEvent('new', bp));
                reply = `breakpoint created`;
            } else {
                const matches = /del +([0-9]+)/.exec(args.expression);
                if (matches && matches.length === 2) {
                    const mbp = this._runtime.clearBreakPoint(sourceFile, this.convertClientLineToDebugger(parseInt(matches[1])));
                    if (mbp) {
                        const bp = new Breakpoint(false) as DebugProtocol.Breakpoint;
                        bp.id= mbp.id;
                        this.sendEvent(new BreakpointEvent('removed', bp));
                        reply = `breakpoint deleted`;
                    }
                } else {
                    const matches = /progress/.exec(args.expression);
                    if (matches && matches.length === 1) {
                        if (this._reportProgress) {
                            reply = `progress started`;
                            this.progressSequence();
                        } else {
                            reply = `frontend doesn't support progress (capability 'supportsProgressReporting' not set)`;
                        }
                    }
                }
            }
        }

        response.body = {
            result: reply ? reply : `evaluate(context: '${args.context}', '${args.expression}')`,
            variablesReference: 0
        };
        this.sendResponse(response);
    }

    private async progressSequence() {

        const ID = '' + this._progressId++;

        await timeout(100);

        const title = this._isProgressCancellable ? 'Cancellable operation' : 'Long running operation';
        const startEvent: DebugProtocol.ProgressStartEvent = new ProgressStartEvent(ID, title);
        startEvent.body.cancellable = this._isProgressCancellable;
        this._isProgressCancellable = !this._isProgressCancellable;
        this.sendEvent(startEvent);
        this.sendEvent(new OutputEvent(`start progress: ${ID}\n`));

        let endMessage = 'progress ended';

        for (let i = 0; i < 100; i++) {
            await timeout(500);
            this.sendEvent(new ProgressUpdateEvent(ID, `progress: ${i}`));
            if (this._cancelledProgressId === ID) {
                endMessage = 'progress cancelled';
                this._cancelledProgressId = undefined;
                this.sendEvent(new OutputEvent(`cancel progress: ${ID}\n`));
                break;
            }
        }
        this.sendEvent(new ProgressEndEvent(ID, endMessage));
        this.sendEvent(new OutputEvent(`end progress: ${ID}\n`));

        this._cancelledProgressId = undefined;
    }

    protected completionsRequest(response: DebugProtocol.CompletionsResponse, args: DebugProtocol.CompletionsArguments): void {

        response.body = {
            targets: [
                {
                    label: "item 10",
                    sortText: "10"
                },
                {
                    label: "item 1",
                    sortText: "01"
                },
                {
                    label: "item 2",
                    sortText: "02"
                },
                {
                    label: "array[]",
                    selectionStart: 6,
                    sortText: "03"
                },
                {
                    label: "func(arg)",
                    selectionStart: 5,
                    selectionLength: 3,
                    sortText: "04"
                }
            ]
        };
        this.sendResponse(response);
    }

    protected cancelRequest(response: DebugProtocol.CancelResponse, args: DebugProtocol.CancelArguments) {
        if (args.requestId) {
            this._cancelationTokens.set(args.requestId, true);
        }
        if (args.progressId) {
            this._cancelledProgressId= args.progressId;
        }
    }

    protected customRequest(command: string, response: DebugProtocol.Response, args: any) {
        if (command === 'toggleFormatting') {
            this._showHex = ! this._showHex;
            if (this._useInvalidatedEvent) {
                this.sendEvent(new InvalidatedEvent( ['variables'] ));
            }
            this.sendResponse(response);
        } else {
            super.customRequest(command, response, args);
        }
    }

    //---- helpers

    private createSource(filePath: string): Source {
        return new Source(basename(filePath), this.convertDebuggerPathToClient(filePath), undefined, undefined, 'squid-adapter-data');
    }
}
