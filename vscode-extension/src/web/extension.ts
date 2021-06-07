/*---------------------------------------------------------
 * Copyright (C) Microsoft Corporation. All rights reserved.
 *--------------------------------------------------------*/

import * as vscode from 'vscode';
import { activateSquidDebug } from '../activateSquidDebug';

export function activate(context: vscode.ExtensionContext) {
	activateSquidDebug(context);
}

export function deactivate() {
	// nothing to do
}
