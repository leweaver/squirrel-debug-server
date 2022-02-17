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
#include <unordered_set>
#include <functional>

using sdb::SquirrelDebugger;
using sdb::data::ImmediateValue;
using sdb::data::PaginationInfo;
using sdb::data::ReturnCode;
using sdb::data::RunState;
using sdb::data::StackEntry;
using sdb::data::Status;
using sdb::data::Variable;

using sdb::sq::CreateChildVariable;
using sdb::sq::CreateChildVariablesFromIterable;
using sdb::sq::WithVariableAtPath;
using sdb::sq::GetObjectFromExpression;
using sdb::sq::ExpressionNode;
using sdb::sq::ExpressionNodeType;
using sdb::sq::ScopedVerifySqTop;
using sdb::sq::WatchParseError;

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
          uint32_t stackFrame, const std::string& path, const PaginationInfo& pagination,
          std::vector<Variable>& stack) const
  {
    if (path.empty())
    {
      // List out locals and free variables
      return WithStackRootVariables(stackFrame, path, pagination, stack, [vm = this->vm](Variable& variable) {
        auto rc = CreateChildVariable(vm, variable);
        // Can't edit locals and free variables right now.
        variable.editable = false;
        return rc;
      });
    }
    else
    {
      return WithStackVariables(
              stackFrame, path,
              [vm = this->vm, &pagination, &stack](const sq::PathPartConstIter& begin, const sq::PathPartConstIter& end) {
                return sq::CreateChildVariablesFromIterable(vm, begin + 1, end, pagination, stack);
              });
    }
  }

  ReturnCode WithStackRootVariables(uint32_t stackFrame, const std::string& path, const PaginationInfo& pagination, std::vector<Variable>& stack, const std::function<ReturnCode(Variable&)>& fn) const
  {
    ReturnCode rc {};
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
      rc = fn(variable);

      // Remove local from stack
      sq_poptop(vm);

      if (rc != ReturnCode::Success) {
        break;
      }

      // Can't edit root variables/globals right now
      variable.editable = false;
      stack.emplace_back(std::move(variable));
    }
    return rc;
  }

  ReturnCode WithStackVariables(uint32_t stackFrame, const std::string& path, const std::function<ReturnCode(const sq::PathPartConstIter&, const sq::PathPartConstIter&)>& fn) const
  {
    if (path.empty()) {
      SDB_LOGE(kLogTag, "WithStackRootVariables must be called when path is empty.");
      return ReturnCode::ErrorInternal;
    }

    ScopedVerifySqTop scopedVerify(vm);
    std::vector<uint64_t> pathParts;
      // Convert comma-separated list to vector
      std::stringstream ss(path);
      while (ss.good()) {
        std::string substr;
        getline(ss, substr, SquirrelDebugger::kPathSeparator);
        pathParts.emplace_back(stoi(substr));
      }

    ReturnCode rc {};
    // Push local with given index to stack
    const auto* const localName = sq_getlocal(vm, stackFrame, *pathParts.begin());
    if (localName == nullptr) {
      SDB_LOGD(kLogTag, "No local with given index: %d", *pathParts.begin());
      return ReturnCode::InvalidParameter;
    }

    rc = fn(pathParts.begin(), pathParts.end());
    if (rc != ReturnCode::Success) {
      SDB_LOGI(kLogTag, "Failed to find stack variables for path: %s", path.c_str());
    }

    // Remove local from stack
    sq_poptop(vm);

    return rc;
  }

  ReturnCode
  PopulateGlobalVariables(const std::string& path, const PaginationInfo& pagination, std::vector<Variable>& stack) const
  {
    ScopedVerifySqTop scopedVerify(vm);

    std::vector<uint64_t> pathParts;
    if (!path.empty()) {
      // Convert comma-separated list to vector
      std::stringstream ss(path);
      while (ss.good()) {
        std::string substr;
        getline(ss, substr, ',');
        pathParts.emplace_back(stoi(substr));
      }
    }

    sq_pushroottable(vm);
    const ReturnCode rc = CreateChildVariablesFromIterable(vm, pathParts.begin(), pathParts.end(), pagination, stack);
    sq_poptop(vm);

    return rc;
  }

  ReturnCode SetStackVariableValue(uint32_t stackFrame, const std::string& path, const std::string& newValueString, data::Variable& newValue) {
    return WithStackVariables(stackFrame, path, [vm=this->vm, &newValueString, &newValue](sq::PathPartConstIter begin, sq::PathPartConstIter end) -> ReturnCode {
      if (begin + 1 == end) {
        // In this case, attempting to set a local var directly. Need to do something else
        // TODO: there's no set equiv of sq_getlocal(), as getlocal contains both local and free variables.
        SDB_LOGE(kLogTag, "SetStackVariableValue: Can't set value of local & function arguments.");
        return ReturnCode::InvalidParameter;
      }
      return sq::WithVariableAtPath(vm, begin + 1, end, [end, vm, &newValueString, &newValue]() -> ReturnCode {
        // obj at -3, key is at -2, current value at -1

        // need to copy the key as it is consumed by the update method.
        HSQOBJECT key = {};
        sq_getstackobj(vm, -2, &key);

        ReturnCode rc = sq::UpdateFromString(vm, -4, newValueString);

        if (ReturnCode::Success != rc) {
          return rc;
        }

        // Re-add key and new value
        sq_pushobject(vm, key);
        sq_pushobject(vm, key); // value replaces the key in the lookup, so need to add key twice
        if (!SQ_SUCCEEDED(sq_get(vm, -4))) {
          SDB_LOGE(kLogTag, "Failed to read new value of property");
          return data::ReturnCode::Invalid;
        }
        return sq::CreateChildVariable(vm, newValue);
      });
    });
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
  if (!eventInterface_)
  {
    SDB_LOGW(kLogTag, "AddVm: No event interface has been added! Events will not be sent.");
  }
  vmData_->vm = vm;
}

void SquirrelDebugger::DetachVm(SQVM* const vm)
{
  SDB_LOGI(kLogTag, "Detaching debugger");
  if (vmData_ != nullptr && vmData_->vm != nullptr) {
    std::lock_guard lock(pauseMutex_);
    pauseMutexData_->isPaused = false; // ensures that public method calls return an error

    if (pauseRequested_ != PauseType::None) {
      // resume execution of the script
      pauseRequested_ = PauseType::None;
      pauseCv_.notify_all();
    }

    vmData_->vm = nullptr;
    vmData_->currentStack.clear();
    vmData_->fileNameHandles.clear();
  }
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
  SDB_LOGD(kLogTag, "cannot continue, not paused.");
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
    SDB_LOGD(kLogTag, "cannot retrieve stack variables, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  if (stackFrame > vmData_->currentStack.size()) {
    SDB_LOGD(kLogTag, "cannot retrieve stack variables, requested stack frame exceeds current stack depth");
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
    SDB_LOGD(kLogTag, "cannot retrieve global variables, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  return vmData_->PopulateGlobalVariables(path, pagination, variables);
}

ReturnCode SquirrelDebugger::SetStackVariableValue(uint32_t stackFrame, const std::string& path, const std::string& newValueString, data::Variable& newValue)
{
  SDB_LOGD(kLogTag, "SetStackVariableValue");
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_->isPaused) {
    SDB_LOGD(kLogTag, "cannot set stack variable, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  if (stackFrame > vmData_->currentStack.size()) {
    SDB_LOGD(kLogTag, "cannot retrieve stack variables, requested stack frame exceeds current stack depth");
    return ReturnCode::InvalidParameter;
  }
  return vmData_->SetStackVariableValue(stackFrame, path, newValueString, newValue);
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

struct RefOwner {
  RefOwner(HSQUIRRELVM vm)
      : vm(vm)
  {}

  ~RefOwner()
  {
    if (vm) {
      for (auto po : objectsToCleanup) {
        sq_release(vm, &po);
      }
    }
  }

  std::vector<HSQOBJECT> objectsToCleanup;
  HSQUIRRELVM vm = nullptr;
};

ReturnCode SquirrelDebugger::GetImmediateValue(
        const int32_t stackFrame, const std::string& watch, const PaginationInfo& pagination,
        ImmediateValue& foundRootVariable)
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

  SDB_LOGD(kLogTag, "Parsed expression OK. Will now evaluate");

  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_->isPaused) {
    SDB_LOGD(kLogTag, "cannot read watch value, not paused.");
    return ReturnCode::InvalidNotPaused;
  }

  // Stack of expression roots, that need to be evaluated by sq::GetObjectFromExpression
  std::deque<ExpressionNode*> stack;
  stack.push_back(expressionRoot.get());

  const auto vm = vmData_->vm;
  auto refOwner = RefOwner(vm);

  // For each node in the expression tree:
  // if it has an accessorExpression, need to evaluate that first: so put it on the stack

  //std::unordered_set<ExpressionNode*> treesWithResolvedAccessorExpressions;
  struct ExpressionNodeState {
    std::unique_ptr<sq::SqExpressionNode> sqNode;
    std::vector<uint32_t> iteratorPath;
    HSQOBJECT resolvedValue;
    data::VariableScope scope;
  };
  std::unordered_map<ExpressionNode*, ExpressionNodeState> expressionResults;

  while (!stack.empty()) {
    ExpressionNode* node = stack.back();

    // Look through the 'next' list and add any root expressions that need evaluating.
    {
      ExpressionNode* nextNode = node;
      bool hasUnresolvedDependencies = false;
      while (nextNode != nullptr) {
        if (nextNode->accessorExpression != nullptr &&
            expressionResults.find(nextNode->accessorExpression.get()) == expressionResults.end())
        {
          // If so, need to make sure that the next node's accessorExpression is resolved first.
          stack.push_back(nextNode->accessorExpression.get());
          hasUnresolvedDependencies = true;
        }
        nextNode = nextNode->next.get();
      }

      if (hasUnresolvedDependencies) {
        continue;
      }
    }

    stack.pop_back();

    // Contains the execution results for this node.

    // Create HSQOBJECT representations of all nodes in this tree
    ExpressionNodeState nodeResult;
    {
      ExpressionNode* nodeIter = node;
      nodeResult.sqNode = std::make_unique<sq::SqExpressionNode>();
      sq::SqExpressionNode* lastSqNode = nodeResult.sqNode.get();
      while (nodeIter != nullptr) {
        auto& targetSqObject = lastSqNode->accessorObject;
        if (nodeIter->accessorExpression != nullptr) {
          auto accessorExpressionResultPos = expressionResults.find(nodeIter->accessorExpression.get());
          if (accessorExpressionResultPos == expressionResults.end()) {
            SDB_LOGE(
                    kLogTag,
                    "Attempting to resolve a root expression where the accessor expression is not yet resolved.");
            return ReturnCode::ErrorInternal;
          }
          targetSqObject = accessorExpressionResultPos->second.resolvedValue;
        }
        else
        {
          if (node->type == ExpressionNodeType::Number) {
            errno = 0;
            const int intVal = strtol(node->accessorValue.c_str(), nullptr, 10);
            if (errno == ERANGE) {
              SDB_LOGD(
                      kLogTag, "expressionNode value %s exceeds maximum parsable integer.",
                      node->accessorValue.c_str());
              return ReturnCode::InvalidParameter;
            }
            sq_pushinteger(vm, intVal);
            sq_getstackobj(vm, -1, &targetSqObject);
            sq_poptop(vm);
          }
          else {
            // It's a string, which is ref counted. So need to make sure we keep it alive, then clean up after ourselves later.
            const SQChar* chars = node->accessorValue.c_str();
            sq_pushstring(vm, chars, node->accessorValue.size());
            sq_getstackobj(vm, -1, &targetSqObject);
            sq_addref(vm, &targetSqObject);
            refOwner.objectsToCleanup.push_back(targetSqObject);
            sq_poptop(vm);
          }
        }

        nodeIter = nodeIter->next.get();
        if (nodeIter != nullptr) {
          lastSqNode->next = std::make_unique<sq::SqExpressionNode>();
        }
        lastSqNode = lastSqNode->next.get();
      }
    }

    if (node->type == ExpressionNodeType::Identifier) {
      auto& iteratorPath = nodeResult.iteratorPath;

      // evaluate this expression accessor
      const std::string& accessorValue = node->accessorValue;
      if (stackFrame >= 0) {
        ScopedVerifySqTop scopedVerify(vm);
        // Is there a local by this name?
        sq_newtable(vm);
        for (SQUnsignedInteger nSeq = 0;; ++nSeq) {
          // Push local with given index to stack
          const auto* const localName = sq_getlocal(vm, stackFrame, nSeq);
          if (localName == nullptr) {
            break;
          }
          if (localName == accessorValue) {

            sq_pushstring(vm, localName, accessorValue.size());
            sq_push(vm, -2);
            sq_rawset(vm, -4);
            sq_poptop(vm);// pop local variable now so that the table is passed in to the expression
            const auto rc = GetObjectFromExpression(
                    vm, nodeResult.sqNode.get(), pagination, nodeResult.resolvedValue, iteratorPath);
            if (rc != ReturnCode::Success) {
              sq_poptop(vm);// pop temp table
              SDB_LOGD(kLogTag, "Failed to read local variable: %s", accessorValue.c_str());
              return rc;
            }

            // The first location in the iterator path will just be the iterator in the temporary table. Conveniently
            // we can just replace this with the iterator to the local variable
            iteratorPath[0] = static_cast<uint32_t>(nSeq);

            nodeResult.scope = data::VariableScope::Local;
            break;
          }

          sq_poptop(vm);// pop local variable
        }
        sq_poptop(vm);// pop temp table
      }

      if (iteratorPath.empty()) {
        sq_pushroottable(vm);
        const auto rc = GetObjectFromExpression(
                vm, nodeResult.sqNode.get(), pagination, nodeResult.resolvedValue, iteratorPath);
        sq_poptop(vm);
        if (rc != ReturnCode::Success) {
          SDB_LOGD(kLogTag, "Failed to read variable from root table: %s", accessorValue.c_str());
          return rc;
        }
        nodeResult.scope = data::VariableScope::Global;
      }
    }
    else
    {
      nodeResult.resolvedValue = nodeResult.sqNode->accessorObject;
      nodeResult.scope = data::VariableScope::Evaluation;
    }

    expressionResults[node] = std::move(nodeResult);
  }

  // Now convert the found SQOBJECT into a variable to return back
  {
    const auto rootResultPos = expressionResults.find(expressionRoot.get());
    if (rootResultPos == expressionResults.end()) {
      SDB_LOGD(kLogTag, "Expression must not be empty.");
      return ReturnCode::InvalidParameter;
    }

    auto& nodeState = rootResultPos->second;

    sq_pushobject(vm, nodeState.resolvedValue);
    CreateChildVariable(vm, foundRootVariable.variable);
    sq_poptop(vm);

    foundRootVariable.iteratorPath = nodeState.iteratorPath;
    foundRootVariable.scope = nodeState.scope;

    return ReturnCode::Success;
  }
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
    SDB_LOGD(kLogTag, "cannot step, not paused.");
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

  if (eventInterface_) {
    eventInterface_->HandleStatusChanged(status);
  }
  return ReturnCode::Success;
}

void SquirrelDebugger::SquirrelNativeDebugHook(
        SQVM* const /*v*/, const SQInteger type, const SQChar* sourceName, const SQInteger line,
        const SQChar* functionName)
{
  if (!vmData_->vm) {
    // Not currently attached.
    return;
  }

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
      if (eventInterface_) {
        eventInterface_->HandleStatusChanged(status);
      }

      // This Cv will be signaled whenever the value of pauseRequested_ changes.
      pauseCv_.wait(lock);
      pauseMutexData_->isPaused = false;
    }
  }
}

void SquirrelDebugger::SquirrelPrintCallback(HSQUIRRELVM vm, const bool isErr, const std::string_view str) const
{
  if (!vmData_->vm) {
    // Not currently attached.
    return;
  }

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
  if (eventInterface_) {
    eventInterface_->HandleOutputLine(outputLine);
  }
}


SQInteger SquirrelDebugger::DefaultStackSize()
{
  return kDefaultStackSize;
}
