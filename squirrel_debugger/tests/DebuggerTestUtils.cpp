#include "DebuggerTestUtils.h"

#include <sqstdio.h>
#include <sqstdmath.h>
#include <sqstdstring.h>
#include <sqstdsystem.h>

using sdb::SquirrelDebugger;
using sdb::data::ReturnCode;
using sdb::data::RunState;

namespace sdb::tests {
class MessageEventInterfaceImpl : public sdb::MessageEventInterface {
 public:
  void GetLastStatus(sdb::data::Status& status)
  {
    std::unique_lock<std::mutex> lock(statusMutex_);
    status = lastStatus_;
  }
  void ResetWaitForStatus()
  {
    receivedStatus_ = false;
  }
  // Returns true if status was found without timeout being reached
  bool WaitForStatus(RunState runState)
  {
    std::unique_lock<std::mutex> lock(statusMutex_);
    if (receivedStatus_ && runState == lastStatus_.runState) {
      return true;
    }

    while (!receivedStatus_ || runState != lastStatus_.runState) {
      auto cvStatus = statusCv_.wait_for(lock, std::chrono::seconds(1));
      if (cvStatus == std::cv_status::timeout) {
        GTEST_NONFATAL_FAILURE_("Reached timeout before test could begin.");
        return false;
      }
    }
    return true;
  }
  void HandleStatusChanged(const sdb::data::Status& status)
  {
    std::unique_lock<std::mutex> lock(statusMutex_);
    receivedStatus_ = true;
    lastStatus_ = status;
    statusCv_.notify_all();
  }
  void HandleOutputLine(const sdb::data::OutputLine& outputLine) {}

 private:
  std::mutex statusMutex_;
  std::condition_variable statusCv_;
  sdb::data::Status lastStatus_;
  bool receivedStatus_ = false;
};

SQInteger SquirrelFileLexFeedAscii(SQUserPointer file);
void SquirrelNativeDebugHook(SQVM* v, SQInteger type, const SQChar* sourceName, SQInteger line, const SQChar* funcName);
void SquirrelPrintCallback(HSQUIRRELVM vm, const SQChar* text, ...);
void SquirrelPrintErrCallback(HSQUIRRELVM vm, const SQChar* text, ...);

void SquirrelDebuggerTest::HandleOutputLine(
        SQVM* const vm, const bool isErr, const SQChar* text, char* const args) const
{
  std::array<char, 1024> buffer;
  const auto size = vsprintf_s(buffer.data(), 1024, text, args);
  if (size > 0) {
    const std::string_view str = {&buffer[0], static_cast<std::string_view::size_type>(size)};
    if (debugger_ != nullptr) {
      debugger_->SquirrelPrintCallback(vm, isErr, str);
    }
  }
}

void SquirrelDebuggerTest::HandleDebugHook(
        SQVM* const v, const SQInteger type, const SQChar* sourceName, const SQInteger line,
        const SQChar* const funcName)
{
  debugger_->SquirrelNativeDebugHook(v, type, sourceName, line, funcName);
}

void SquirrelDebuggerTest::SetUp()
{
  debugger_ = std::make_unique<SquirrelDebugger>();
  eventInterface_ = std::make_shared<MessageEventInterfaceImpl>();
  debugger_->SetEventInterface(eventInterface_);

  GTEST_ASSERT_EQ(nullptr, gInstance);
  gInstance = this;

  CreateVm();
}

void SquirrelDebuggerTest::TearDown()
{
  if (debugger_) {
    debugger_->DetachVm(vm_);
    eventInterface_ = {};
  }
  if (squirrelWorker_.joinable()) {
    squirrelWorker_.join();
  }
  debugger_ = {};

  if (vm_) {
    sq_close(vm_);
    vm_ = nullptr;
  }
  gInstance = nullptr;
}

void SquirrelDebuggerTest::CreateVm()
{
  vm_ = sq_open(SquirrelDebugger::DefaultStackSize());

  // Forward squirrel print & errors streams
  sq_setprintfunc(vm_, SquirrelPrintCallback, SquirrelPrintErrCallback);

  // Enable debugging hooks
  debugger_->AddVm(vm_);
  const auto rc = debugger_->PauseExecution();
  ASSERT_EQ(ReturnCode::Success, rc);

  sq_enabledebuginfo(vm_, SQTrue);
  sq_setnativedebughook(vm_, &SquirrelNativeDebugHook);

  // Register stdlibs
  sq_pushroottable(vm_);
  sqstd_register_iolib(vm_);
  sqstd_register_mathlib(vm_);
  sqstd_register_stringlib(vm_);
  sqstd_register_systemlib(vm_);
}

void SquirrelDebuggerTest::RunAndPauseTestFile(const char* const testFileName)
{
  {
    FILE* fpRaw = nullptr;
    fopen_s(&fpRaw, testFileName, "rb");
    ASSERT_TRUE(fpRaw != nullptr) << "Test file must exist";

    const std::unique_ptr<std::FILE, decltype(&std::fclose)> fp = {fpRaw, &std::fclose};
    const auto res = sq_compile(vm_, SquirrelFileLexFeedAscii, fp.get(), testFileName, 1);
    ASSERT_TRUE(SQ_SUCCEEDED(res)) << "Test file must compile successfully";
  }

  sq_pushroottable(vm_);

  auto taskWorker = [vm = vm_]() {
    if (!SQ_SUCCEEDED(sq_call(vm, 1 /* root table */, SQFalse, SQTrue))) {
      GTEST_NONFATAL_FAILURE_("Failed to execute script");
    }
    sq_pop(vm, 1);// Pop function
  };
  squirrelWorker_ = std::thread(taskWorker);

  eventInterface_->WaitForStatus(RunState::Paused);
}

void SquirrelDebuggerTest::RunAndPauseTestFileAtLine(
        const char* const testFileName, const sdb::data::CreateBreakpoint& bp)
{
  RunAndPauseTestFile(testFileName);

  std::vector<sdb::data::CreateBreakpoint> createBps;
  createBps.push_back(bp);
  std::vector<sdb::data::ResolvedBreakpoint> resolvedBps;
  ASSERT_EQ(ReturnCode::Success, GetDebugger().SetFileBreakpoints(testFileName, createBps, resolvedBps));

  ResetWaitForStatus();
  ASSERT_EQ(ReturnCode::Success, GetDebugger().ContinueExecution());
  WaitForStatus(RunState::Paused);
}

SquirrelDebugger& SquirrelDebuggerTest::GetDebugger()
{
  return *debugger_.get();
}

void SquirrelDebuggerTest::ResetWaitForStatus()
{
  eventInterface_->ResetWaitForStatus();
}
bool SquirrelDebuggerTest::WaitForStatus(RunState runState)
{
  return eventInterface_->WaitForStatus(runState);
}
void SquirrelDebuggerTest::GetLastStatus(sdb::data::Status& status)
{
  eventInterface_->GetLastStatus(status);
}

SQInteger SquirrelFileLexFeedAscii(SQUserPointer file)
{
  char c = 0;
  if (fread(&c, sizeof(c), 1, static_cast<FILE*>(file)) > 0) {
    return c;
  }
  return 0;
}
void SquirrelNativeDebugHook(
        SQVM* const v, const SQInteger type, const SQChar* sourceName, const SQInteger line,
        const SQChar* const funcName)
{
  SquirrelDebuggerTest::Instance().HandleDebugHook(v, type, sourceName, line, funcName);
}
void SquirrelPrintCallback(HSQUIRRELVM vm, const SQChar* text, ...)
{
  va_list vl;
  va_start(vl, text);
  SquirrelDebuggerTest::Instance().HandleOutputLine(vm, false, text, vl);
  va_end(vl);
}

void SquirrelPrintErrCallback(HSQUIRRELVM vm, const SQChar* text, ...)
{
  va_list vl;
  va_start(vl, text);
  SquirrelDebuggerTest::Instance().HandleOutputLine(vm, true, text, vl);
  va_end(vl);
}
SquirrelDebuggerTest* SquirrelDebuggerTest::gInstance = nullptr;
}

namespace sdb::log {
inline int vasprintf(char** strp, const char* format, va_list ap)
{
  int len = _vscprintf(format, ap);
  if (len < 0)
  {
    return -1;
  }

  char* str = static_cast<char*>(malloc(len + 1));
  if (!str)
  {
    return -1;
  }

#if defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR)
  int retval = _vsnprintf(str, len + 1, format, ap);
#else
  int retval = _vsnprintf_s(str, len + 1, len, format, ap);
#endif
  if (retval < 0)
  {
    free(str);
    return -1;
  }

  *strp = str;
  return retval;
}

void LogString(const char* tag, const size_t line, const Level level, const char* str)
{
  static constexpr std::array<const char*, 5> levelNames {
          "Verbose", "Debug"," Info", "Warning","Error"
  };
  std::cout <<  "[" << levelNames.at(size_t(level)) << "] " << tag << ":" << line << " " << str << std::endl;
}
void LogFormatted(const char* tag, const size_t line, const Level level, const char* message, ...)
{
  va_list ap;
  va_start(ap, message);

  char* str = nullptr;
  int len = vasprintf(&str, message, ap);
  static_cast<void>(len);
  va_end(ap);

  LogString(tag, line, level, str);

  free(str);
}
}// namespace sdb::log