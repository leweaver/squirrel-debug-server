#include "include/sdb/SquirrelDebugger.h"

#include <sdb/LogInterface.h>

#include "BreakpointMap.h"
#include "SquirrelVmHelpers.h"

#include <squirrel.h>

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <deque>
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
using sdb::data::ImmediateValue;

using sdb::sq::CreateChildVariable;
using sdb::sq::CreateChildVariablesFromIterable;
using sdb::sq::CreateChildVariablesFromExpression;
using sdb::sq::ScopedVerifySqTop;
using sdb::sq::WatchParseError;
using sdb::sq::ExpressionNode;
using sdb::sq::ExpressionNodeType;

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
      uint32_t line = 0;

      if (si.line > 0 && si.line <= INT32_MAX) {
        line = static_cast<uint32_t>(si.line);
      }
      stack.push_back({std::string(si.source), line, std::string(si.funcname)});
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

  HSQUIRRELVM vm = nullptr;

  struct StackInfo {
    BreakpointMap::FileNameHandle fileNameHandle;
    SQInteger line;
  };
  std::vector<StackInfo> currentStack;
  std::unordered_map<const SQChar*, BreakpointMap::FileNameHandle> fileNameHandles;
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
        const uint32_t stackFrame, const std::string& path, const PaginationInfo& pagination,
        std::vector<Variable>& variables)
{
  SDB_LOGD(kLogTag, "GetStackVariables");
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_->isPaused) {
    SDB_LOGD(__FILE__, "cannot retrieve stack variables, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  if (stackFrame > vmData_->currentStack.size()) {
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

void PrintNode(ExpressionNode* node)
{
  while (node) {
    if (node->accessorValue.empty()) {
      SDB_LOGD(kLogTag, "[");
      PrintNode(node->accessorExpression.get());
      SDB_LOGD(kLogTag, "]");
    }
    else {
      SDB_LOGD(kLogTag, "type: %d, value: %s", node->type, node->accessorValue.c_str());
    }
    node = node->next.get();
  }
}

ReturnCode SquirrelDebugger::GetImmediateValue(
        const uint32_t stackFrame, const std::string& watch, const PaginationInfo& pagination,
        ImmediateValue& foundVariable)
{
  SDB_LOGD(kLogTag, "GetImmediateValue stackFrame=%" PRIu32 " watch=%s", stackFrame, watch.c_str());

  // We run our own mini-lexer here as we don't want to allow full SQ execution. Just want to find a variable to inspect.

  // Parse the watch string before locking

  std::unique_ptr<ExpressionNode> expressionRoot;
  try {
    auto pos = watch.begin();
    expressionRoot = sq::ParseExpression(pos, watch.end());
    if (pos != watch.end()) {
      throw WatchParseError("Invalid content after the end of the parsed expression", pos);
    }
  }
  catch (const WatchParseError& err) {
    const auto offset = err.pos - watch.begin();
    auto underArrow = std::string(offset, ' ') + "^";
    SDB_LOGD(
            kLogTag, "Failed to parse expression at offset %" PRIu64 " (%s):\n%s\n%s", offset, err.what(),
            watch.c_str(), underArrow.c_str());
    return ReturnCode::InvalidParameter;
  }

  SDB_LOGD(kLogTag, "Parsed OK, much wow!");
  PrintNode(expressionRoot.get());

  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_->isPaused) {
    SDB_LOGD(kLogTag, "cannot read watch value, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  // Evaluate all sub-expressions
  std::deque<ExpressionNode*> stack;
  stack.push_back(expressionRoot.get());

  const auto vm = vmData_->vm;
  while (!stack.empty()) {
    ExpressionNode* node = stack.back();
    if (node->accessorExpression != nullptr)
    {
      stack.push_back(node->accessorExpression.get());
    }
    else {

      stack.pop_back();

      // evaluate this expression - which has NO sub expressions.
      std::vector<Variable> variables;

      {
        ScopedVerifySqTop scopedVerify(vm);
        // Is there a local by this name?
        sq_newtable(vm);
        for (SQUnsignedInteger nSeq = 0;; ++nSeq) {
          // Push local with given index to stack
          const auto* const localName = sq_getlocal(vm, stackFrame, nSeq);
          if (localName == nullptr) {
            break;
          }
          if (localName == node->accessorValue) {
            sq_pushstring(vm, localName, node->accessorValue.size());
            sq_push(vm, -2);
            sq_rawset(vm, -4);
            sq_poptop(vm);// pop local variable now so that the table is passed in to the expression
            const auto rc = CreateChildVariablesFromExpression(vm, node->next.get(), pagination, variables);
            if (rc != ReturnCode::Success) {
              SDB_LOGD(kLogTag, "Failed to read local variable: %s", node->accessorValue.c_str());
              return rc;
            }
            foundVariable.scope = data::VariableScope::Local;
            break;
          }

          sq_poptop(vm);// pop local variable
        }
        sq_poptop(vm); // pop temp table

        if (variables.empty()) {
          sq_pushroottable(vm);
          const auto rc = CreateChildVariablesFromExpression(vm, node, pagination, variables);
          sq_poptop(vm);
          if (rc != ReturnCode::Success) {
            SDB_LOGD(kLogTag, "Failed to read variable from root table: %s", node->accessorValue.c_str());
            return rc;
          }
          foundVariable.scope = data::VariableScope::Global;
        }
      }
      
      if (variables.size() != 1) {
        SDB_LOGE(kLogTag, "Expected exacly 1 variable for last expressionNode %s", expressionRoot->accessorValue.c_str());
        return ReturnCode::ErrorInternal;
      }

      if (stack.empty())
      {
        foundVariable.variable = variables.at(0);
        return ReturnCode::Success;
      }

      const auto& variable = variables.at(0);
      if (variable.valueType == data::VariableType::Integer) {
        node->type = ExpressionNodeType::Number;
      }
      else if (variable.valueType == data::VariableType::String) {
        node->type = ExpressionNodeType::String;
      }
      else {
        SDB_LOGD(kLogTag, "Expression must resolve to a string or number.");
        return ReturnCode::InvalidParameter;
      }
      node->accessorValue = variable.value;
      node->accessorExpression.reset();
    }
  }

  return ReturnCode::Success;
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
    if (id == 0ULL) {
      SDB_LOGD(kLogTag, "SetFileBreakpoints Invalid field 'id', must be > 0");
    }
    else if (line == 0U) {
      SDB_LOGD(kLogTag, "SetFileBreakpoints Invalid field 'line', must be > 0");
    }
    else {
      bps.emplace_back(Breakpoint{id, line});

      // todo: load file from disk, make sure line isn't empty.
      resolvedBps.emplace_back(data::ResolvedBreakpoint{id, line, true});

      continue;
    }

    return ReturnCode::InvalidParameter;
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

  eventInterface_->HandleStatusChanged(status);
  return ReturnCode::Success;
}

void SquirrelDebugger::SquirrelNativeDebugHook(
        SQVM* const /*v*/, const SQInteger type, const SQChar* sourceName, const SQInteger line,
        const SQChar* functionName)
{
  // 'c' called when a function has been called
  if (type == 'c') {
    const auto fileNameHandlePos = vmData_->fileNameHandles.find(sourceName);
    BreakpointMap::FileNameHandle fileNameHandle;
    if (fileNameHandlePos == vmData_->fileNameHandles.end()) {
      std::unique_lock lock(pauseMutex_);
      fileNameHandle = pauseMutexData_->breakpoints.EnsureFileNameHandle(sourceName);
      vmData_->fileNameHandles.emplace(sourceName, fileNameHandle);
    }
    else {
      fileNameHandle = fileNameHandlePos->second;
    }

    assert(vmData_->currentStack.size() < kDefaultStackSize);
    vmData_->currentStack.emplace_back(internal::SquirrelVmDataImpl::StackInfo{fileNameHandle, line});

    if (pauseRequested_ != PauseType::None) {
      if (pauseMutexData_->returnsRequired >= 0) {
        ++pauseMutexData_->returnsRequired;
      }
    }
    // 'r' called when a function returns
  }
  else if (type == 'r') {
    assert(!vmData_->currentStack.empty());
    vmData_->currentStack.pop_back();
    if (pauseRequested_ != PauseType::None) {
      std::unique_lock lock(pauseMutex_);
      if (pauseRequested_ != PauseType::None) {
        --pauseMutexData_->returnsRequired;
      }
    }
    // 'l' called every line(that contains some code)
  }
  else if (type == 'l') {

    Breakpoint bp = {};
    auto& currentStackHead = vmData_->currentStack.back();
    currentStackHead.line = line;
    const auto& handle = currentStackHead.fileNameHandle;

    std::unique_lock lock(pauseMutex_);

    // Check for breakpoints
    if (line >= 0 && line < INT32_MAX && handle != nullptr &&
        pauseMutexData_->breakpoints.ReadBreakpoint(handle, static_cast<uint32_t>(line), bp))
    {
      // right now, only support basic breakpoints so no further interrogation is needed.
      pauseMutexData_->returnsRequired = 0;
      pauseRequested_ = PauseType::Pause;
    }

    // Pause the thread if necessary
    if (pauseRequested_ != PauseType::None && pauseMutexData_->returnsRequired <= 0) {
      pauseMutexData_->isPaused = true;

      auto& status = pauseMutexData_->status;
      status.runState = RunState::Paused;
      status.pausedAtBreakpointId = bp.id;

      vmData_->PopulateStack(status.stack);

      eventInterface_->HandleStatusChanged(status);

      // This Cv will be signaled whenever the value of pauseRequested_ changes.
      pauseCv_.wait(lock);
      pauseMutexData_->isPaused = false;
    }
  }
}

void SquirrelDebugger::SquirrelPrintCallback(HSQUIRRELVM vm, const bool isErr, const std::string_view str) const
{
  const auto& stackInfo = vmData_->currentStack.back();
  uint32_t line = 0;
  if (stackInfo.line > 0 && stackInfo.line <= INT32_MAX) {
    line = static_cast<uint32_t>(stackInfo.line);
  }

  const data::OutputLine outputLine{
          str,
          isErr,
          std::string_view(stackInfo.fileNameHandle.get()->data(), stackInfo.fileNameHandle.get()->size()),
          line,
  };
  eventInterface_->HandleOutputLine(outputLine);
}


SQInteger SquirrelDebugger::DefaultStackSize()
{
  return kDefaultStackSize;
}
