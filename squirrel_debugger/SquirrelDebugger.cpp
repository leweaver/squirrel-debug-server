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

using sdb::sq::createChildVariable;
using sdb::sq::createChildVariablesFromIterable;
using sdb::sq::ScopedVerifySqTop;

using LockGuard = std::lock_guard<std::recursive_mutex>;

// Kinda just chosen arbitrarily - but if this isn't big enough, you should seriously consider changing your algorithm!
const long long kMaxStackDepth = 100;

namespace sdb {
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
  void populateStack(std::vector<StackEntry>& stack) const
  {
    stack.clear();

    SQStackInfos si;
    auto stackIdx = 0;
    while (SQ_SUCCEEDED(sq_stackinfos(vm, stackIdx, &si))) {
      stack.push_back({std::string(si.source), si.line, std::string(si.funcname)});
      ++stackIdx;
    }
  }

  ReturnCode populateStackVariables(
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
        rc = createChildVariable(vm, variable);

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
      rc = createChildVariablesFromIterable(vm, pathParts.begin() + 1, pathParts.end(), pagination, stack);
      if (rc != ReturnCode::Success) {
        SDB_LOGI(__FILE__, "Failed to find stack variables for path: %s", path.c_str());
      }

      // Remove local from stack
      sq_poptop(vm);
    }

    return rc;
  }
  ReturnCode
  populateGlobalVariables(const std::string& path, const PaginationInfo& pagination, std::vector<Variable>& stack) const
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
    const ReturnCode rc = createChildVariablesFromIterable(vm, pathParts.begin(), pathParts.end(), pagination, stack);
    sq_poptop(vm);

    return rc;
  }

  int currentStackDepth = 0;
  HSQUIRRELVM vm = nullptr;
};

}// namespace sdb

constexpr char* kLogTag = "SquirrelDebugger";

SquirrelDebugger::SquirrelDebugger()
    : pauseMutexData_(new PauseMutexDataImpl())
    , vmData_(new SquirrelVmDataImpl())
{
  SDB_LOGD(kLogTag, "Initialized");
  SDB_LOGD(kLogTag, "Initialized %s", "a thing");
}

SquirrelDebugger::~SquirrelDebugger()
{
  delete pauseMutexData_;
  delete vmData_;
}

void SquirrelDebugger::setEventInterface(std::shared_ptr<MessageEventInterface> eventInterface)
{
  eventInterface_ = std::move(eventInterface);
}

void SquirrelDebugger::addVm(SQVM* const vm)
{
  // TODO: Multiple VM support
  vmData_->vm = vm;
}

ReturnCode SquirrelDebugger::pauseExecution()
{
  if (pauseRequested_ == PauseType::None) {
    std::lock_guard lock(pauseMutex_);
    if (pauseRequested_ == PauseType::None) {
      pauseRequested_ = PauseType::Pause;
      pauseMutexData_->returnsRequired = -1;
    }
  }
  return ReturnCode::Success;
}

ReturnCode SquirrelDebugger::continueExecution()
{
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

ReturnCode SquirrelDebugger::stepOut()
{
  return Step(PauseType::StepOut, 1);
}

ReturnCode SquirrelDebugger::stepOver()
{
  return Step(PauseType::StepOver, 0);
}

ReturnCode SquirrelDebugger::stepIn()
{
  return Step(PauseType::StepIn, -1);
}

ReturnCode SquirrelDebugger::getStackVariables(
        int32_t stackFrame, const std::string& path, const data::PaginationInfo& pagination,
        std::vector<Variable>& variables)
{
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_->isPaused) {
    SDB_LOGD(__FILE__, "cannot retrieve stack variables, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  if (stackFrame > vmData_->currentStackDepth) {
    SDB_LOGD(__FILE__, "cannot retrieve stack variables, requested stack frame exceeds current stack depth");
    return ReturnCode::InvalidParameter;
  }
  return vmData_->populateStackVariables(stackFrame, path, pagination, variables);
}

ReturnCode SquirrelDebugger::getGlobalVariables(
        const std::string& path, const PaginationInfo& pagination, std::vector<Variable>& variables)
{
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_->isPaused) {
    SDB_LOGD(__FILE__, "cannot retrieve global variables, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  return vmData_->populateGlobalVariables(path, pagination, variables);
}

ReturnCode SquirrelDebugger::setFileBreakpoints(
        const std::string& file, const std::vector<data::CreateBreakpoint>& createBps,
        std::vector<data::ResolvedBreakpoint>& resolvedBps)
{
  // First resolve the breakpoints against the script file.
  std::vector<Breakpoint> bps;
  for (const auto& createBp : createBps) {
    bps.emplace_back(Breakpoint{createBp.line});

    // todo: load file from disk, make sure line isn't empty.
    resolvedBps.emplace_back(data::ResolvedBreakpoint{createBp.id, createBp.line, true});
  }

  {
    std::lock_guard lock(pauseMutex_);

    pauseMutexData_->breakpoints.clear(file);
    pauseMutexData_->breakpoints.addAll(file, bps);
  }

  return ReturnCode::Success;
}

ReturnCode SquirrelDebugger::Step(PauseType pauseType, int returnsRequired)
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

ReturnCode SquirrelDebugger::sendStatus()
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

  eventInterface_->onStatus(std::move(status));
  return ReturnCode::Success;
}

void SquirrelDebugger::squirrelNativeDebugHook(
        SQVM* const v, const SQInteger type, const SQChar* sourceName, const SQInteger line, const SQChar* functionName)
{
  auto& pauseMutexData = *pauseMutexData_;
  auto& vmData = *vmData_;

  // 'c' called when a function has been called
  if (type == 'c') {
    ++vmData.currentStackDepth;
    assert(vmData.currentStackDepth < kMaxStackDepth);
    if (pauseRequested_ != PauseType::None) {
      std::unique_lock lock(pauseMutex_);
      if (pauseRequested_ != PauseType::None) {
        if (pauseMutexData.returnsRequired >= 0) {
          ++pauseMutexData.returnsRequired;
        }
      }
    }
    // 'r' called when a function returns
  }
  else if (type == 'r') {
    --vmData.currentStackDepth;
    assert(vmData.currentStackDepth >= 0);
    if (pauseRequested_ != PauseType::None) {
      std::unique_lock lock(pauseMutex_);
      if (pauseRequested_ != PauseType::None) {
        --pauseMutexData.returnsRequired;
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
      if (line >= 0 && line < INT32_MAX &&
          pauseMutexData_->breakpoints.readBreakpoint(fileName, static_cast<uint32_t>(line), bp))
      {
        // right now, only support basic breakpoints so no further interrogation is needed.
        pauseRequested_ = PauseType::Pause;
      }
    }

    if (pauseRequested_ != PauseType::None && pauseMutexData.returnsRequired <= 0) {
      std::unique_lock lock(pauseMutex_);
      if (pauseRequested_ != PauseType::None && pauseMutexData.returnsRequired <= 0) {
        pauseMutexData.isPaused = true;

        auto& status = pauseMutexData.status;
        status.runState = RunState::Paused;

        vmData.populateStack(status.stack);

        {
          Status statusCopy = status;
          eventInterface_->onStatus(std::move(statusCopy));
        }

        // This Cv will be signaled whenever the value of pauseRequested_ changes.
        pauseCv_.wait(lock);
        pauseMutexData.isPaused = false;
      }
    }
  }
}
