#include "include/sdb/SquirrelDebugger.h"

#include <sdb/LogInterface.h>

#include "BreakpointMap.h"
#include "SquirrelVmHelpers.h"

#include <squirrel.h>

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <mutex>
#include <sstream>
#include <unordered_map>

using sdb::SquirrelDebugger;
using sdb::data::PaginationInfo;
using sdb::data::ReturnCode;
using sdb::data::RunState;
using sdb::data::StackEntry;
using sdb::data::Status;
using sdb::data::Variable;

using sdb::sq::CreateChildVariable;
using sdb::sq::CreateChildVariablesFromIterable;
using sdb::sq::ScopedVerifySqTop;

using LockGuard = std::lock_guard<std::recursive_mutex>;

// Kinda just chosen arbitrarily - but if this isn't big enough, you should seriously consider changing your algorithm!
const SQInteger kDefaultStackSize = 1024;

const char* const kLogTag = "SquirrelDebugger";

namespace sdb::internal {
struct PauseMutexDataImpl {
  bool isPaused = false;

  // How many levels of the stack must be popped before we break
  int returnsRequired = 0;

  // The status as it was last time the application paused.
  Status status = {};

  // Loaded breakpoints
  BreakpointMap breakpoints = {};
};

struct SquirrelVmDataImpl {
  void PopulateStack(std::vector<StackEntry>& stack) const
  {
    stack.clear();

    SQStackInfos si;
    auto stackIdx = 0;
    while (SQ_SUCCEEDED(sq_stackinfos(vm, stackIdx, &si))) {
      stack.push_back({std::string(si.source), si.line, std::string(si.funcname)});
      ++stackIdx;
    }
  }

  ReturnCode PopulateStackVariables(
          int32_t stackFrame, const std::string& path, const PaginationInfo& pagination,
          std::vector<Variable>& stack) const
  {
    ScopedVerifySqTop scopedVerify(vm);

    std::vector<uint64_t> pathParts;
    if (!path.empty()) {
      // Convert comma-separated list to vector
      std::stringstream s_stream(path);
      while (s_stream.good()) {
        std::string substr;
        getline(s_stream, substr, ',');
        pathParts.emplace_back(stoi(substr));
      }
    }

    ReturnCode rc = ReturnCode::Success;
    if (pathParts.begin() == pathParts.end()) {
      const auto maxNSeq = pagination.beginIterator + pagination.count;
      for (SQUnsignedInteger nSeq = pagination.beginIterator; nSeq < maxNSeq; ++nSeq) {
        // Push local with given index to stack
        const auto* const localName = sq_getlocal(vm, stackFrame, nSeq);
        if (localName == nullptr) {
          break;
        }

        Variable variable;
        variable.pathIterator = nSeq;
        variable.pathUiString = localName;
        rc = CreateChildVariable(vm, variable);

        // Remove local from stack
        sq_poptop(vm);

        if (rc != ReturnCode::Success) {
          break;
        }
        stack.emplace_back(std::move(variable));
      }
    }
    else {
      // Push local with given index to stack
      const auto* const localName = sq_getlocal(vm, stackFrame, *pathParts.begin());
      if (localName == nullptr) {
        SDB_LOGD(__FILE__, "No local with given index: %d", *pathParts.begin());
        return ReturnCode::InvalidParameter;
      }

      //variable.name = *(pathParts.end() - 1);
      rc = CreateChildVariablesFromIterable(vm, pathParts.begin() + 1, pathParts.end(), pagination, stack);
      if (rc != ReturnCode::Success) {
        SDB_LOGI(__FILE__, "Failed to find stack variables for path: %s", path.c_str());
      }

      // Remove local from stack
      sq_poptop(vm);
    }

    return rc;
  }
  ReturnCode
  PopulateGlobalVariables(const std::string& path, const PaginationInfo& pagination, std::vector<Variable>& stack) const
  {
    ScopedVerifySqTop scopedVerify(vm);

    std::vector<uint64_t> pathParts;
    if (!path.empty()) {
      // Convert comma-separated list to vector
      std::stringstream s_stream(path);
      while (s_stream.good()) {
        std::string substr;
        getline(s_stream, substr, ',');
        pathParts.emplace_back(stoi(substr));
      }
    }

    sq_pushroottable(vm);
    const ReturnCode rc = CreateChildVariablesFromIterable(vm, pathParts.begin(), pathParts.end(), pagination, stack);
    sq_poptop(vm);

    return rc;
  }

  int currentStackDepth = 0;
  HSQUIRRELVM vm = nullptr;
};

}// namespace sdb::internal

SquirrelDebugger::SquirrelDebugger()
    : pauseMutexData_(new internal::PauseMutexDataImpl())
    , vmData_(new internal::SquirrelVmDataImpl())
{}

SquirrelDebugger::~SquirrelDebugger()
{
  delete pauseMutexData_;
  delete vmData_;
}

void SquirrelDebugger::SetEventInterface(std::shared_ptr<MessageEventInterface> eventInterface)
{
  eventInterface_ = std::move(eventInterface);
}

void SquirrelDebugger::AddVm(SQVM* const vm)
{
  // TODO: Multiple VM support
  vmData_->vm = vm;
}

ReturnCode SquirrelDebugger::PauseExecution()
{
  SDB_LOGD(kLogTag, "PauseExecution");
  if (pauseRequested_ == PauseType::None) {
    std::lock_guard lock(pauseMutex_);
    if (pauseRequested_ == PauseType::None) {
      pauseRequested_ = PauseType::Pause;
      pauseMutexData_->returnsRequired = -1;
    }
  }
  return ReturnCode::Success;
}

ReturnCode SquirrelDebugger::ContinueExecution()
{
  SDB_LOGD(kLogTag, "ContinueExecution");
  if (pauseRequested_ != PauseType::None) {
    std::lock_guard lock(pauseMutex_);
    if (pauseRequested_ != PauseType::None) {
      pauseRequested_ = PauseType::None;
      pauseCv_.notify_all();
      return ReturnCode::Success;
    }
  }
  SDB_LOGD(__FILE__, "cannot continue, not paused.");
  return ReturnCode::InvalidNotPaused;
}

ReturnCode SquirrelDebugger::StepOut()
{
  SDB_LOGD(kLogTag, "StepOut");
  return Step(PauseType::StepOut, 1);
}

ReturnCode SquirrelDebugger::StepOver()
{
  SDB_LOGD(kLogTag, "StepOver");
  return Step(PauseType::StepOver, 0);
}

ReturnCode SquirrelDebugger::StepIn()
{
  SDB_LOGD(kLogTag, "StepIn");
  return Step(PauseType::StepIn, -1);
}

ReturnCode SquirrelDebugger::GetStackVariables(
        int32_t stackFrame, const std::string& path, const data::PaginationInfo& pagination,
        std::vector<Variable>& variables)
{
  SDB_LOGD(kLogTag, "GetStackVariables");
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_->isPaused) {
    SDB_LOGD(__FILE__, "cannot retrieve stack variables, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  if (stackFrame > vmData_->currentStackDepth) {
    SDB_LOGD(__FILE__, "cannot retrieve stack variables, requested stack frame exceeds current stack depth");
    return ReturnCode::InvalidParameter;
  }
  return vmData_->PopulateStackVariables(stackFrame, path, pagination, variables);
}

ReturnCode SquirrelDebugger::GetGlobalVariables(
        const std::string& path, const PaginationInfo& pagination, std::vector<Variable>& variables)
{
  SDB_LOGD(kLogTag, "GetGlobalVariables");
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_->isPaused) {
    SDB_LOGD(__FILE__, "cannot retrieve global variables, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  return vmData_->PopulateGlobalVariables(path, pagination, variables);
}

ReturnCode SquirrelDebugger::SetFileBreakpoints(
        const std::string& file, const std::vector<data::CreateBreakpoint>& createBps,
        std::vector<data::ResolvedBreakpoint>& resolvedBps)
{
  SDB_LOGD(
          kLogTag, "SetFileBreakpoints file=%s createBps.size()=%" PRIu64, file.c_str(),
          static_cast<uint64_t>(createBps.size()));
  // First resolve the breakpoints against the script file.
  std::vector<Breakpoint> bps;
  for (const auto& [id, line] : createBps) {
    bps.emplace_back(Breakpoint{line});

    // todo: load file from disk, make sure line isn't empty.
    resolvedBps.emplace_back(data::ResolvedBreakpoint{id, line, true});
  }

  {
    std::lock_guard lock(pauseMutex_);

    const auto handle = pauseMutexData_->breakpoints.EnsureFileNameHandle(file);
    pauseMutexData_->breakpoints.Clear(handle);
    pauseMutexData_->breakpoints.AddAll(handle, bps);
  }

  return ReturnCode::Success;
}

ReturnCode SquirrelDebugger::Step(const PauseType pauseType, const int returnsRequired)
{
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_->isPaused) {
    SDB_LOGD(__FILE__, "cannot step, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  pauseMutexData_->returnsRequired = returnsRequired;
  pauseRequested_ = pauseType;
  pauseCv_.notify_all();

  return ReturnCode::Success;
}

ReturnCode SquirrelDebugger::SendStatus()
{
  // Don't allow un-pause while we read the status.
  Status status;
  {
    std::lock_guard lock(pauseMutex_);
    if (pauseRequested_ != PauseType::None) {
      if (pauseMutexData_->isPaused) {
        // Make a copy of the last known status.
        status = pauseMutexData_->status;
        status.runState = RunState::Paused;
      }
      else if (pauseRequested_ == PauseType::Pause) {
        status.runState = RunState::Pausing;
      }
      else {
        status.runState = RunState::Stepping;
      }
    }
    else {
      status.runState = RunState::Running;
    }
  }

  eventInterface_->HandleStatusChanged(std::move(status));
  return ReturnCode::Success;
}

void SquirrelDebugger::SquirrelNativeDebugHook(
        SQVM* const /*v*/, const SQInteger type, const SQChar* sourceName, const SQInteger line,
        const SQChar* /*functionName*/)
{
  // 'c' called when a function has been called
  if (type == 'c') {
    ++vmData_->currentStackDepth;
    assert(vmData_->currentStackDepth < kDefaultStackSize);
    if (pauseRequested_ != PauseType::None) {
      std::unique_lock lock(pauseMutex_);
      if (pauseRequested_ != PauseType::None) {
        if (pauseMutexData_->returnsRequired >= 0) {
          ++pauseMutexData_->returnsRequired;
        }
      }
    }
    // 'r' called when a function returns
  }
  else if (type == 'r') {
    --vmData_->currentStackDepth;
    assert(vmData_->currentStackDepth >= 0);
    if (pauseRequested_ != PauseType::None) {
      std::unique_lock lock(pauseMutex_);
      if (pauseRequested_ != PauseType::None) {
        --pauseMutexData_->returnsRequired;
      }
    }
    // 'l' called every line(that contains some code)
  }
  else if (type == 'l') {
    if (pauseRequested_ == PauseType::None) {
      std::unique_lock lock(pauseMutex_);
      // Check for breakpoints
      Breakpoint bp;
      const std::string fileName{sourceName};

      // TODO: store a map of const char* to FileNameHandle. (after verifying that the const char* addresses don't constantly change...)
      const auto handle = pauseMutexData_->breakpoints.FindFileNameHandle(fileName);
      if (line >= 0 && line < INT32_MAX && handle != nullptr &&
          pauseMutexData_->breakpoints.ReadBreakpoint(handle, static_cast<uint32_t>(line), bp))
      {
        // right now, only support basic breakpoints so no further interrogation is needed.
        pauseRequested_ = PauseType::Pause;
      }
    }

    if (pauseRequested_ != PauseType::None && pauseMutexData_->returnsRequired <= 0) {
      std::unique_lock lock(pauseMutex_);
      if (pauseRequested_ != PauseType::None && pauseMutexData_->returnsRequired <= 0) {
        pauseMutexData_->isPaused = true;

        auto& status = pauseMutexData_->status;
        status.runState = RunState::Paused;

        vmData_->PopulateStack(status.stack);

        {
          Status statusCopy = status;
          eventInterface_->HandleStatusChanged(std::move(statusCopy));
        }

        // This Cv will be signaled whenever the value of pauseRequested_ changes.
        pauseCv_.wait(lock);
        pauseMutexData_->isPaused = false;
      }
    }
  }
}

SQInteger SquirrelDebugger::DefaultStackSize()
{
  return kDefaultStackSize;
}
