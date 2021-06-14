'use strict';


export enum EventMessageType {
    status = 0
}

export class EventMessage {
    public type: EventMessageType = EventMessageType.status;
    public message?: any = undefined;

    constructor(instanceData?: any) {
        this.type = EventMessageType[instanceData.type as keyof typeof EventMessageType];
        this.message = instanceData.message;
    }
}

export enum Runstate {
    running = 0,
    pausing = 1,
    paused = 2
}

export class StackEntry {
    public file: string = "";
    public line: number = 0;
    public function: string = "";

    constructor(instanceData?: any) {
        if (instanceData) {
            this.file = instanceData.file;
            this.line = instanceData.line;
            this.function = instanceData.function;
        }
    }
}

export class Status {
    public runstate: Runstate = Runstate.running;
    public stack: StackEntry[] = [];

    constructor(instanceData?: any) {
        if (instanceData) {
            this.runstate = Runstate[instanceData.runstate as keyof typeof Runstate];
            this.stack = (instanceData.stack ?? []).map(d => new StackEntry(d));
        }
    }
}