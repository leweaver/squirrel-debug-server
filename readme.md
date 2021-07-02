# [S]quirrel Debugger

## Release Notes - v0.1
First versioned release, 'MVP'

[x] Websocket listener for server state changes
[x] HTTP Command interface
[x] Stack-Local variables
[x] Global Variables
[x] Simple Breakpoints
[x] Output redirection & capture
[x] Coding standards and formatting
[x] Better debug logging
[x] Run sample exe with arguments
[ ] Hover-evaluation
[ ] Copyright headers

## Not Supported
[ ] Multiple VM's (threads)
[ ] Improved output of variables in inspector
[ ] Conditional breakpoints
[ ] Modification of variable values (int and string only?)
[ ] Immediate window for execution
[ ] MacOS / Linux support
[ ] squirrel unicode builds

Setup:

1. download and install any toolchain dependencies (see following toolchains section)
1. restart your IDE if it was already open
1. open the top-level CMakeLists.txt in either CLion or Visual Studio
1. Set up the toolchain

# Toolchain
On windows, there are a few different ways to build. First is using the Visual Studio toolchain (MSVC or Clang); and the
second is using MinGW/GCC.

## MinGW Toolchain Details
Need to install the 64bit version of mingw. The MinGW installation manager doesn't seem to support x64, so need to download just the plain installer: http://mingw-w64.org/doku.php/download/mingw-builds

In the MinGW-w64 installation wizard, 
- make sure to select the required architecture to x86_64.  (Note that the default suggested option is 32-bit.)
- Set threads to posix (otherwise you won't have std::mutex etc)

For instructions on using the latter with CLion, see their [setup guide](https://www.jetbrains.com/help/clion/quick-tutorial-on-configuring-clion-on-windows.html).

(tl;dr, add a new MinGW toolchain with Environment: `C:\Program Files\mingw-w64\x86_64-8.1.0-posix-seh-rt_v6-rev0\mingw64`)

# VS-Code Extension

## Pre-requisites
Is a package manager for windows. Install: https://chocolatey.org/ and install the following things
in an administrative powershell:

NodeJS LTS: `choco install nodejs-lts`
Yarn: `choco install yarn`



## Building
More Details: https://code.visualstudio.com/api/extension-guides/debugger-extension#the-mock-debug-extension

```
yarn
```

## Run Unit Tests
```
cd vscode-extension
yarn test
```
