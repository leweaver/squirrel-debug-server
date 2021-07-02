import * as vscode from "vscode";

const CONFIG_CONTAINER = "squid_tools";

export function getConfiguration(name: string, defaultValue: any = null) {
	return vscode.workspace.getConfiguration(CONFIG_CONTAINER).get(name, defaultValue) || defaultValue;
}

export function setConfiguration(name: string, value: any) {
	return vscode.workspace.getConfiguration(CONFIG_CONTAINER).update(name, value);
}

export function isDebugMode(): boolean {
	return process.env.VSCODE_DEBUG_MODE === "true";
}