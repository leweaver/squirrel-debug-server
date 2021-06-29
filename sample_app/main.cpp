
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

constexpr auto kPLogInstanceId = PLOG_DEFAULT_INSTANCE_ID;

SQInteger File_LexFeedAscii(SQUserPointer file)
{
  char c = 0;
  if (fread(&c, sizeof(c), 1, static_cast<FILE*>(file)) > 0) {
    return c;
  }
  return 0;
}

SQRESULT CompileFile(SQVM* const v, const char* filename)
{
  FILE* fpRaw = nullptr;
  fopen_s(&fpRaw, filename, "rb");
  if (fpRaw != nullptr) {
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> fp = {fpRaw, &std::fclose};
    const auto res = sq_compile(v, File_LexFeedAscii, fp.get(), filename, 1);
    if (SQ_FAILED(res)) {
      cerr << "Failed to compile" << endl;
    }
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
        SQVM* const v, const SQInteger type, const SQChar* sourceName, const SQInteger line,
        const SQChar* const funcName)
{
  auto iter = std::find_if(vms.begin(), vms.end(), [v](const auto& vmInfo) { return vmInfo.v == v; });
  assert(iter != vms.end());
  iter->debugger->SquirrelNativeDebugHook(v, type, sourceName, line, funcName);
}

void Run(std::shared_ptr<SquirrelDebugger> debugger)
{
  HSQUIRRELVM v = sq_open(SquirrelDebugger::DefaultStackSize());
  {
    std::lock_guard lock(vmsMutex);
    vms.push_back({v, debugger});
  }

  // Forward squirrel print & errors to cout/cerr
  sq_setprintfunc(v, SquirrelPrintCallback, SquirrelPrintErrCallback);

  // File to Run
  const std::string fileName = R"(C:\repos\vscode-quirrel-debugger\sample_app\scripts\hello_world.nut)";

  // Enable debugging hooks
  if (debugger) {
    debugger->AddVm(v);
    const std::vector<sdb::data::CreateBreakpoint> bps{{0U, 43U}};
    std::vector<sdb::data::ResolvedBreakpoint> resolvedBps;
    const auto rc = debugger->SetFileBreakpoints(fileName, bps, resolvedBps);
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
    const auto pos = std::find_if(vms.begin(), vms.end(), [v](const auto& vmInfo) { return vmInfo.v == v; });
    const auto lastPos = vms.end() - 1;
    std::swap(*pos, *lastPos);
    vms.pop_back();
  }
}

namespace sdb::log {
const std::array<plog::Severity, 5> kLevelToSeverity = {plog::verbose, plog::debug, plog::info, plog::warning, plog::error};
void LogFormatted(const char* tag, const size_t line, const Level level, const char* message, ...)
{
  const auto severity = kLevelToSeverity.at(static_cast<std::size_t>(level));
  IF_PLOG_(PLOG_DEFAULT_INSTANCE_ID, severity)
  {
    va_list ap;
    va_start(ap, message);

    char* str = nullptr;
    int len = plog::util::vasprintf(&str, message, ap);
    static_cast<void>(len);
    va_end(ap);
    *plog::get<kPLogInstanceId>() +=
            plog::Record(severity, tag, line, PLOG_GET_FILE(), PLOG_GET_THIS(), kPLogInstanceId).ref() << str;

    free(str);
  }
}
void LogString(const char* tag, const size_t line, const Level level, const char* str)
{
  const auto severity = kLevelToSeverity.at(static_cast<std::size_t>(level));
  IF_PLOG_(PLOG_DEFAULT_INSTANCE_ID, severity)
  {
    *plog::get<kPLogInstanceId>() +=
            plog::Record(severity, tag, line, PLOG_GET_FILE(), PLOG_GET_THIS(), kPLogInstanceId).ref() << str;
  }
}
}// namespace sdb::log

template<typename T> void WrapWithTries(bool& ioShouldContinue, T& oStream, const char* failMessageDetail, const std::function<void()>& fn)
{
  if (!ioShouldContinue) {
    return;
  }
  try {
    fn();
  }
  catch (const std::exception& e) {
    try {
      oStream << "Uncaught exception (" << failMessageDetail << "): " << e.what();
    }
    catch (...) {
    }
    ioShouldContinue = false;
  }
  catch (...) {
    try {
      oStream << "Failed to initialize logger (" << failMessageDetail << ")";
    }
    catch (...) {
    }
    ioShouldContinue = false;
  }
}

int main(int argc, char* argv[])
{
  std::unique_ptr<EmbeddedServer> ep;
  std::shared_ptr<SquirrelDebugger> debugger;

  bool shouldContinue = true;

  WrapWithTries(shouldContinue, cerr, "Initializing Logger", [&]() {
    // plog requires that appenders are static, thus destroyed at exit time.
    static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;  // NOLINT(clang-diagnostic-exit-time-destructors)
    plog::init(plog::verbose, &consoleAppender);
  });

  WrapWithTries(shouldContinue, cerr, "Initializing Environment", [&]() {
    EmbeddedServer::InitEnvironment();
    ep.reset(EmbeddedServer::Create());

    debugger = std::make_shared<SquirrelDebugger>();
    ep->SetCommandInterface(debugger);
    debugger->SetEventInterface(ep->GetEventInterface());

    ep->Start();
  });

  WrapWithTries(shouldContinue, cerr, "Running", [&]() {
    Run(debugger);
  });

  WrapWithTries(shouldContinue, cerr, "Teardown", [&]() {
    if (ep != nullptr) {
      ep->Stop(true);
      ep.reset();
    }

    EmbeddedServer::ShutdownEnvironment();
  });

  return shouldContinue ? 0 : 1;
}