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
    paused = 2,
    stepping = 3
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

export enum  VariableType { string, bool, integer, float, closure, class, instance, array, table, other, null };

export class Variable {
    public pathIterator: number;
    public pathUiString: string;
    public pathTableKeyType: VariableType;
    public valueType: VariableType;
    public value: string;
    public valueRawAddress: number;
    public childCount: number;
    public instanceClassName: string;

    constructor(instanceData?: any) {
        if (instanceData) {
            this.pathIterator = instanceData.pathIterator;
            this.pathUiString = instanceData.pathUiString;
            this.pathTableKeyType = VariableType[instanceData.pathTableKeyType as keyof typeof VariableType];
            this.valueType = VariableType[instanceData.valueType as keyof typeof VariableType];
            this.value = instanceData.value;
            this.valueRawAddress = instanceData.valueRawAddress;
            this.childCount = instanceData.childCount;
            this.instanceClassName = instanceData.instanceClassName;
        } else {
            this.pathIterator = 0;
            this.pathUiString = "";
            this.pathTableKeyType = VariableType.null;
            this.valueType = VariableType.null;
            this.value = "";
            this.valueRawAddress = 0;
            this.childCount = 0;
            this.instanceClassName = "";
        }
    }
}