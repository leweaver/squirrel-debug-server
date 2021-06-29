
#include <squirrel.h>

#include <sdb/EmbeddedServer.h>
#include <sdb/LogInterface.h>
#include <sdb/MessageInterface.h>
#include <sdb/SquirrelDebugger.h>

#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Init.h>
#include <plog/Log.h>
#include <plog/Util.h>

#include <sqstdio.h>
#include <sqstdmath.h>
#include <sqstdstring.h>
#include <sqstdsystem.h>

#include <array>
#include <cassert>
#include <cstdarg>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

using sdb::EmbeddedServer;
using sdb::SquirrelDebugger;
using sdb::data::ReturnCode;
using std::cerr;
using std::cout;
using std::endl;

void compile() {}

SQInteger File_LexFeedAscii(SQUserPointer file)
{
  char c;
  if (fread(&c, sizeof(c), 1, static_cast<FILE*>(file)) > 0) return c;
  return 0;
}

SQRESULT CompileFile(HSQUIRRELVM v, const char* filename)
{
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

void SquirrelOnCompileError(
        HSQUIRRELVM /*v*/, const SQChar* desc, const SQChar* source, const SQInteger line, const SQInteger column)
{
  cerr << "Failed to compile script: " << source << ": " << line << " (col " << column << ")" << desc;
}

void SquirrelPrintCallback(HSQUIRRELVM vm, const SQChar* text, ...)
{
  char buffer[1024];

  va_list vl;
  va_start(vl, text);
  vsprintf_s(buffer, 1024, text, vl);
  va_end(vl);

  cout << buffer << endl;
}

void SquirrelPrintErrCallback(HSQUIRRELVM vm, const SQChar* text, ...)
{
  char buffer[1024];

  va_list vl;
  va_start(vl, text);
  vsprintf_s(buffer, 1024, text, vl);
  va_end(vl);

  cerr << buffer << endl;
}


struct VmInfo {
  HSQUIRRELVM v;
  std::shared_ptr<SquirrelDebugger> debugger;
};
std::vector<VmInfo> vms;
std::mutex vmsMutex;

void SquirrelNativeDebugHook(
        HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line, const SQChar* funcname)
{
  auto iter = std::find_if(vms.begin(), vms.end(), [v](const auto& vmInfo) { return vmInfo.v == v; });
  assert(iter != vms.end());
  iter->debugger->squirrelNativeDebugHook(v, type, sourcename, line, funcname);
}

void run(std::shared_ptr<SquirrelDebugger> debugger)
{
  HSQUIRRELVM v = sq_open(1024);//creates a VM with initial stack size 1024
  {
    std::lock_guard lock(vmsMutex);
    vms.push_back({v, debugger});
  }

  // Forward squirrel print & errors to cout/cerr
  sq_setprintfunc(v, SquirrelPrintCallback, SquirrelPrintErrCallback);

  // File to run
  const std::string fileName = R"(C:\repos\vscode-quirrel-debugger\sample_app\scripts\hello_world.nut)";

  // Enable debugging hooks
  if (debugger) {
    debugger->addVm(v);
    //if (ReturnCode::Success != debugger->Pause()) { cerr << "Failed to pause on startup." << endl; }
    const std::vector<sdb::data::CreateBreakpoint> bps{{0U, 43U}};
    std::vector<sdb::data::ResolvedBreakpoint> resolvedBps;
    const auto rc = debugger->setFileBreakpoints(fileName, bps, resolvedBps);
    if (rc != ReturnCode::Success) {
      cerr << "Failed to set BP on startup";
    }
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
  sq_setcompilererrorhandler(v, SquirrelOnCompileError);
  if (SQ_SUCCEEDED(CompileFile(v, fileName.c_str()))) {
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

namespace sdb::log {
std::array<plog::Severity, 5> kLevelToSeverity = {plog::verbose, plog::debug, plog::info, plog::warning, plog::error};
void logFormatted(const char* tag, const size_t line, const Level level, const char* message, ...)
{
  constexpr auto instanceId = PLOG_DEFAULT_INSTANCE_ID;
  const auto severity = kLevelToSeverity.at(static_cast<std::size_t>(level));
  IF_PLOG_(PLOG_DEFAULT_INSTANCE_ID, severity)
  {
    va_list ap;
    va_start(ap, message);

    char* str = NULL;
    int len = plog::util::vasprintf(&str, message, ap);
    static_cast<void>(len);
    va_end(ap);
    *plog::get<instanceId>() +=
            plog::Record(severity, tag, line, PLOG_GET_FILE(), PLOG_GET_THIS(), instanceId).ref() << str;

    free(str);
  }
}
void logString(const char* tag, const size_t line, const Level level, const char* str)
{
  constexpr auto instanceId = PLOG_DEFAULT_INSTANCE_ID;
  const auto severity = kLevelToSeverity.at(static_cast<std::size_t>(level));
  IF_PLOG_(PLOG_DEFAULT_INSTANCE_ID, severity)
  {
    *plog::get<instanceId>() += plog::Record(severity, tag, line, PLOG_GET_FILE(), PLOG_GET_THIS(), instanceId).ref()
                                << str;
  }
}
}// namespace sdb::log

int main(int argc, char* argv[])
{
  static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;
  plog::init(plog::verbose, &consoleAppender);

  EmbeddedServer::InitEnvironment();

  std::unique_ptr<EmbeddedServer> ep(EmbeddedServer::Create());
  auto debugger = std::make_shared<SquirrelDebugger>();

  ep->SetCommandInterface(debugger);
  debugger->setEventInterface(ep->GetEventInterface());

  ep->Start();

  run(debugger);

  ep->Stop(true);
  ep.reset();

  EmbeddedServer::ShutdownEnvironment();

  return 0;
}