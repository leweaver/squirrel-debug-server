
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

#include <tclap/CmdLine.h>

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

SQInteger SquirrelFileLexFeedAscii(SQUserPointer file)
{
  char c = 0;
  if (fread(&c, sizeof(c), 1, static_cast<FILE*>(file)) > 0) {
    return c;
  }
  return 0;
}

void SquirrelOnCompileError(
        HSQUIRRELVM /*v*/, const SQChar* desc, const SQChar* source, const SQInteger line, const SQInteger column)
{
  PLOGE << "Failed to compile script: " << source << ": " << line << " (col " << column << ")" << desc;
}

void SquirrelPrintCallback(HSQUIRRELVM vm, const SQChar* text, ...);

void SquirrelPrintErrCallback(HSQUIRRELVM vm, const SQChar* text, ...);

void SquirrelNativeDebugHook(SQVM* v, SQInteger type, const SQChar* sourceName, SQInteger line, const SQChar* funcName);

/**
 * A singleton that contains app globals.
 */
class SampleApp {
 public:
  struct InitArgs {
    uint16_t debuggerPort = 8000U;
  };
  struct RunArgs {
    std::string file;
    bool breakOnStart = false;
  };

  static SampleApp& Instance()
  {
    static SampleApp instance;
    return instance;
  }

  void Initialize(const InitArgs& args)
  {
    EmbeddedServer::InitEnvironment();
    ep_.reset(EmbeddedServer::Create(args.debuggerPort));

    debugger_ = std::make_shared<SquirrelDebugger>();
    ep_->SetCommandInterface(debugger_);
    debugger_->SetEventInterface(ep_->GetEventInterface());

    ep_->Start();
  }

  void Run(const RunArgs& args)
  {
    HSQUIRRELVM v = sq_open(SquirrelDebugger::DefaultStackSize());
    {
      std::lock_guard lock(vmsMutex_);
      runningVms_.push_back({v, debugger_});
    }

    // Forward squirrel print & errors streams
    sq_setprintfunc(v, SquirrelPrintCallback, SquirrelPrintErrCallback);

    // Enable debugging hooks
    if (debugger_) {
      debugger_->AddVm(v);
      if (args.breakOnStart) {
        const auto rc = debugger_->PauseExecution();
        if (rc != ReturnCode::Success) {
          cerr << "Failed to pause on startup";
        }
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
    if (SQ_SUCCEEDED(CompileFile(v, args.file.c_str()))) {
      sq_pushroottable(v);

      if (SQ_FAILED(sq_call(v, 1 /* root table */, SQFalse, SQTrue))) {
        cerr << "Failed to call global method" << endl;
      }
      sq_pop(v, 1);// Pop function
    }

    // All done
    sq_close(v);

    {
      std::lock_guard<std::mutex> lock(vmsMutex_);
      const auto pos =
              std::find_if(runningVms_.begin(), runningVms_.end(), [v](const auto& vmInfo) { return vmInfo.v == v; });
      const auto lastPos = runningVms_.end() - 1;
      std::swap(*pos, *lastPos);
      runningVms_.pop_back();
    }
  }

  void Teardown()
  {
    if (ep_ != nullptr) {
      ep_->Stop(true);
      ep_.reset();
    }

    EmbeddedServer::ShutdownEnvironment();
  }

  void HandleOutputLine(SQVM* const vm, const bool isErr, const SQChar* text, char* const args) const
  {
    std::array<char, 1024> buffer;
    const auto size = vsprintf_s(buffer.data(), 1024, text, args);
    if (size > 0) {
      const std::string_view str = {&buffer[0], static_cast<std::string_view::size_type>(size)};

      auto* const debugger = DebuggerForVm(vm);
      if (debugger != nullptr) {
        debugger->SquirrelPrintCallback(vm, isErr, str);
      }

      if (isErr) {
        PLOGE << str;
      }
      else {
        PLOGI << str;
      }
    }
  }

  SquirrelDebugger* DebuggerForVm(SQVM* const vm) const
  {
    const auto iter =
            std::find_if(runningVms_.begin(), runningVms_.end(), [vm](const auto& vmInfo) { return vmInfo.v == vm; });
    return iter != runningVms_.end() ? iter->debugger.get() : nullptr;
  }

 private:
  SampleApp() = default;

  static SQRESULT CompileFile(SQVM* const v, const char* filename)
  {
    FILE* fpRaw = nullptr;
    fopen_s(&fpRaw, filename, "rb");
    if (fpRaw != nullptr) {
      const std::unique_ptr<std::FILE, decltype(&std::fclose)> fp = {fpRaw, &std::fclose};
      const auto res = sq_compile(v, SquirrelFileLexFeedAscii, fp.get(), filename, 1);
      if (SQ_FAILED(res)) {
        PLOGE << "Failed to compile" << endl;
      }
      return res;
    }
    PLOGE << "File doesn't exist" << endl;
    return SQ_ERROR;
  }

  std::unique_ptr<EmbeddedServer> ep_;
  std::shared_ptr<SquirrelDebugger> debugger_;

  struct VmInfo {
    HSQUIRRELVM v;
    std::shared_ptr<SquirrelDebugger> debugger;
  };
  std::vector<VmInfo> runningVms_;
  std::mutex vmsMutex_;
};

void SquirrelPrintCallback(HSQUIRRELVM vm, const SQChar* text, ...)
{
  va_list vl;
  va_start(vl, text);
  SampleApp::Instance().HandleOutputLine(vm, false, text, vl);
  va_end(vl);
}

void SquirrelPrintErrCallback(HSQUIRRELVM vm, const SQChar* text, ...)
{
  va_list vl;
  va_start(vl, text);
  SampleApp::Instance().HandleOutputLine(vm, true, text, vl);
  va_end(vl);
}

void SquirrelNativeDebugHook(
        SQVM* const v, const SQInteger type, const SQChar* sourceName, const SQInteger line,
        const SQChar* const funcName)
{
  SquirrelDebugger* debugger = SampleApp::Instance().DebuggerForVm(v);
  if (debugger != nullptr) {
    debugger->SquirrelNativeDebugHook(v, type, sourceName, line, funcName);
  }
}

namespace sdb::log {
const std::array<plog::Severity, 5> kLevelToSeverity = {
        plog::verbose, plog::debug, plog::info, plog::warning, plog::error};
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

template<typename T>
void WrapWithTries(bool& ioShouldContinue, T& oStream, const char* failMessageDetail, const std::function<void()>& fn)
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

int main(int argc, char** argv)
{
  bool shouldContinue = true;

  WrapWithTries(shouldContinue, cerr, "Initializing Logger", [&]() {
    // plog requires that appenders are static, thus destroyed at exit time.
    static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;// NOLINT(clang-diagnostic-exit-time-destructors)
    plog::init(plog::verbose, &consoleAppender);
  });

  auto initArgs = SampleApp::InitArgs{};
  auto runArgs = SampleApp::RunArgs{};
  WrapWithTries(shouldContinue, cerr, "Reading arguments", [argc, argv, &initArgs, &runArgs]() {
    try {
      TCLAP::CmdLine cmd("Command description message", ' ', "0.9");
      TCLAP::ValueArg fileArg("f", "file", "Squirrel file to run", true, runArgs.file, "string");
      cmd.add(fileArg);
      const TCLAP::SwitchArg breakOnStart(
              "s", "stop_on_start", "If set, the script will pause execution on the first line", cmd,
              runArgs.breakOnStart);
      TCLAP::ValueArg portArg(
              "p", "port", "Network port which the debugger will listen on", false, initArgs.debuggerPort,
              "unsigned integer");
      cmd.add(portArg);

      cmd.parse(argc, argv);

      runArgs.file = fileArg.getValue();
      runArgs.breakOnStart = breakOnStart.getValue();
      initArgs.debuggerPort = portArg.getValue();
    }
    catch (TCLAP::ArgException& e) {
      std::stringstream ss;
      ss << "error: " << e.error() << " for arg " << e.argId() << std::endl;
      throw std::runtime_error(ss.str());
    }
  });

  WrapWithTries(shouldContinue, cerr, "Initializing Environment", [&initArgs] {
    SampleApp::Instance().Initialize(initArgs);
  });

  WrapWithTries(shouldContinue, cerr, "Running", [&runArgs] { SampleApp::Instance().Run(runArgs); });

  WrapWithTries(shouldContinue, cerr, "Teardown", [] { SampleApp::Instance().Teardown(); });

  return shouldContinue ? 0 : 1;
}