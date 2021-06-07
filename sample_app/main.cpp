#include <squirrel.h>
#include "debugger/QuirrelDebugger.h"

#include "debug-server/Endpoint.h"
#include "debug-server/MessageInterface.h"

#include <sqstdio.h>
#include <sqstdmath.h>
#include <sqstdstring.h>
#include <sqstdsystem.h>

#include <array>
#include <cstdarg>
#include <iostream>
#include <map>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <assert.h>

using std::cerr;
using std::cout;
using std::endl;
using qdb::Endpoint;

void compile() {}

SQInteger File_LexFeedAscii(SQUserPointer file) { 
  char c;
  if (fread(&c, sizeof(c), 1, static_cast<FILE*>(file)) > 0)
    return c; 
  return 0;
}

SQRESULT CompileFile(HSQUIRRELVM v, const char* filename) {
  auto* f = fopen(filename, "rb");
  if (f) {
    const auto res = sq_compile(v, File_LexFeedAscii, f, filename, 1);
    if (SQ_FAILED(res)) {
      cerr << "Failed to compile" << endl;
    }
    fclose(f);
    return res;
  }
  cerr << "File doesn't exist" << endl;
  return SQ_ERROR;
}

void SquirrelOnCompileError(HSQUIRRELVM /*v*/, const SQChar* desc, const SQChar* source,
                            const SQInteger line, const SQInteger column) {
  cerr << "Failed to compile script: " << source << ": " << line << " (col " << column << ")" << desc;
}

void SquirrelPrintCallback(HSQUIRRELVM vm, const SQChar* text, ...) {
  char buffer[1024];

  va_list vl;
  va_start(vl, text);
  vsprintf_s(buffer, 1024, text, vl);
  va_end(vl);

  cout << buffer << endl;
}

void SquirrelPrintErrCallback(HSQUIRRELVM vm, const SQChar* text, ...) {
  char buffer[1024];

  va_list vl;
  va_start(vl, text);
  vsprintf_s(buffer, 1024, text, vl);
  va_end(vl);

  cerr << buffer << endl;
}


struct VmInfo {
  HSQUIRRELVM v;
  std::shared_ptr<QuirrelDebugger> debugger;
};
std::vector<VmInfo> vms;
std::mutex vmsMutex;

void SquirrelNativeDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line, const SQChar* funcname) {
  auto iter = std::find_if(vms.begin(), vms.end(), [v](const auto& vmInfo) { return vmInfo.v == v; });
  assert(iter != vms.end());
  iter->debugger->SquirrelNativeDebugHook(v, type, sourcename, line, funcname);
}

void run(std::shared_ptr<QuirrelDebugger> debugger) {
  HSQUIRRELVM v = sq_open(1024);//creates a VM with initial stack size 1024
  {
    std::lock_guard<std::mutex> lock(vmsMutex);
    vms.push_back({v, debugger});
  }

  // Forward squirrel print & errors to cout/cerr
  sq_setprintfunc(v, SquirrelPrintCallback, SquirrelPrintErrCallback);

  // Enable debugging hooks
  if (debugger) {
    debugger->SetVm(v);
    debugger->Pause();
    sq_enabledebuginfo(v, SQTrue);
    sq_setnativedebughook(v, &SquirrelNativeDebugHook);
  }

  // Register stdlibs
  sq_pushroottable(v);
  sqstd_register_iolib(v);
  sqstd_register_mathlib(v);
  sqstd_register_stringlib(v);
  sqstd_register_systemlib(v);

  // Load and execute file
  const auto filename = "C:\\repos\\vscode-quirrel-debugger\\sample_app\\scripts\\hello_world.nut";
  sq_setcompilererrorhandler(v, SquirrelOnCompileError);
  if (SQ_SUCCEEDED(CompileFile(v, filename))) {
    sq_pushroottable(v);

    if (SQ_FAILED(sq_call(v, 1 /* root table */, SQFalse, SQTrue))) {
      cerr << "Failed to call global method" << endl;
    }
    sq_pop(v, 1);// Pop function
  }

  // All done
  sq_close(v);

  {
    std::lock_guard<std::mutex> lock(vmsMutex);
    auto pos = std::find_if(vms.begin(), vms.end(), [v](const auto& vmInfo) { return vmInfo.v == v; });
    auto last_pos = vms.end() - 1;
    std::swap(*pos, *last_pos);
    vms.pop_back();
  }
}

int main(int argc, char* argv[]) {
  
  Endpoint::InitEnvironment();

  std::unique_ptr<Endpoint> ep(Endpoint::Create());
  auto debugger = std::make_shared<QuirrelDebugger>();

  ep->SetCommandInterface(debugger);
  debugger->SetEventInterface(ep->GetEventInterface());

  ep->Start();

  run(debugger);

  ep->Stop(true);
  ep.reset();

  Endpoint::ShutdownEnvironment();

  return 0;
}