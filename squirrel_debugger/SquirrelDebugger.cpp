#include "include/sdb/SquirrelDebugger.h"

#include <squirrel.h>

#include <array>
#include <cassert>
#include <cstdarg>
#include <mutex>
#include <sstream>
#include <unordered_map>

using sdb::SquirrelDebugger;
using sdb::data::ReturnCode;
using sdb::data::Runstate;
using sdb::data::StackEntry;
using sdb::data::Status;
using sdb::data::Variable;
using sdb::data::VariableType;

using LockGuard = std::lock_guard<std::recursive_mutex>;

const long long kMaxTablePrettyPrintSize = 5LL;
const long long pooaxTablePrettyPrintSize = 5LL;
const long long ARC_BOAT = 5LL;
const long long ARC_BOAT2 = 5LL;

// Kinda just chosen arbitrarily - but if this isn't big enough, you should seriously consider changing your algorithm!
const long long kMaxStackDepth = 100;

const char* GetSqObjTypeName(SQObjectType sqType) {
  static const std::array<const char*, 18> typeNames = {
          "NULL",   "INTEGER",   "FLOAT",   "BOOL",          "STRING",    "TABLE",
          "ARRAY",  "USERDATA",  "CLOSURE", "NATIVECLOSURE", "GENERATOR", "USERPOINTER",
          "THREAD", "FUNCPROTO", "CLASS",   "INSTANCE",      "WEAKREF",   "OUTER",
  };
  // Get index of least sig set bit:
#ifdef _MSC_VER
  unsigned idx = __lzcnt(_RAW_TYPE(static_cast<unsigned>(sqType)));
#else
  auto idx = __builtin_ffs(_RAW_TYPE(sqType));
#endif
  return typeNames.at(31 - idx);
}

bool g_allowRecursion = true;

void GetClassesFullNameHelper(HSQUIRRELVM v, const std::string& currentNamespace,
                              std::unordered_map<SQHash, std::string>& classNames) {
  if (sq_gettype(v, -1) != OT_TABLE) { throw std::runtime_error("Must have a table at the top of the stack."); }

  sq_pushnull(v);

  // Iterate over the table.
  while (SQ_SUCCEEDED(sq_next(v, -2))) {
    // What's the type of the VALUE?
    const auto type = sq_gettype(v, -1);
    if (type == OT_TABLE || type == OT_CLASS) {
      const ::SQChar* key;
      sq_getstring(v, -2, &key);
      auto newNamespace = currentNamespace;
      if (!currentNamespace.empty()) { newNamespace.append("."); }
      newNamespace.append(key);

      if (type == OT_CLASS) {
        const auto classHash = sq_gethash(v, -1);
        if (classNames.find(classHash) != classNames.end()) { throw std::runtime_error("class already added man"); }
        classNames[classHash] = newNamespace;
      } else {
        GetClassesFullNameHelper(v, newNamespace, classNames);
      }
    }
    sq_pop(v, 2);
  }

  sq_pop(v, 1);//pops the null iterator
}

std::string GetClassFullName(HSQUIRRELVM v);

VariableType sdb_sq_typeof(HSQUIRRELVM v) {
  switch (sq_gettype(v, sq_gettop(v))) {
    case OT_BOOL: 
      return VariableType::Bool;
    case OT_INTEGER:
      return VariableType::Integer;
    case OT_FLOAT:
      return VariableType::Float;
    case OT_STRING:
      return VariableType::String;
    case OT_CLOSURE:
      return VariableType::Closure;
    case OT_CLASS:
      return VariableType::Class;
    case OT_INSTANCE:
      return VariableType::Instance;
    case OT_ARRAY:
      return VariableType::Array;
    case OT_TABLE:
      return VariableType::Table;
    default:
      return VariableType::Other;
  }
}

std::string sdb_sq_toString(HSQUIRRELVM v) {
  std::stringstream ss;
  const auto topIdx = sq_gettop(v);
  switch (const auto type = sq_gettype(v, topIdx); type) {
    case OT_BOOL: {
      SQBool val = SQFalse;
      if (SQ_SUCCEEDED(sq_getbool(v, topIdx, &val))) { ss << (val == SQTrue ? "true" : "false"); }
      break;
    }
    case OT_INTEGER: {
      SQInteger val = 0;
      if (SQ_SUCCEEDED(sq_getinteger(v, topIdx, &val))) { ss << val; }
      break;
    }
    case OT_FLOAT: {
      SQFloat val = 0.0f;
      if (SQ_SUCCEEDED(sq_getfloat(v, topIdx, &val))) { ss << val; }
      break;
    }
    case OT_STRING: {
      const ::SQChar* val = nullptr;
      if (SQ_SUCCEEDED(sq_getstring(v, topIdx, &val))) { ss << val; }
      break;
    }
    case OT_CLOSURE: {
      if (SQ_SUCCEEDED(sq_getclosurename(v, topIdx))) {
        const ::SQChar* val = nullptr;
        if (SQ_SUCCEEDED(sq_getstring(v, topIdx + 1, &val))) {
          ss << GetSqObjTypeName(type) << (val != nullptr ? val : "(anonymous)");

          // pop name of closure
          sq_poptop(v);
          break;
        }
      }
      ss << "Invalid Closure";
      break;
    }
    case OT_CLASS: {
      ss << GetClassFullName(v);
      break;
    }
    case OT_INSTANCE: {
      if (SQ_SUCCEEDED(sq_getclass(v, topIdx))) {
        ss << "Instance of " << sdb_sq_toString(v);
        sq_poptop(v);
      } else {
      }
      break;
    }
    case OT_ARRAY:
    case OT_TABLE: {
      const auto arrSize = sq_getsize(v, topIdx);

      //null iterator
      sq_pushnull(v);
      ss << GetSqObjTypeName(type);
      ss << " (len=" << arrSize << ")";

      sq_pop(v, 1);//pops the null iterator
      break;
    }
    default:
      ss << GetSqObjTypeName(type);
  }

  return ss.str();
}

// Pretty prints the var at the top of the stack.
// Useful for debugging.
std::string prettyPrint(HSQUIRRELVM v) {
  std::stringstream ss;
  const auto topIdx = sq_gettop(v);
  const auto type = sq_gettype(v, topIdx);
  switch (type) {
    case OT_BOOL: {
      SQBool val;
      sq_getbool(v, topIdx, &val);
      ss << (val ? "true" : "false");
      break;
    }
    case OT_INTEGER: {
      SQInteger val;
      sq_getinteger(v, topIdx, &val);
      ss << val;
      break;
    }
    case OT_FLOAT: {
      SQFloat val;
      sq_getfloat(v, topIdx, &val);
      ss << val;
      break;
    }
    case OT_STRING: {
      const ::SQChar* val;
      sq_getstring(v, topIdx, &val);
      ss << '"' << val << '"';
      break;
    }
    case OT_CLOSURE: {
      if (SQ_SUCCEEDED(sq_getclosurename(v, topIdx))) {
        const ::SQChar* val;
        if (SQ_SUCCEEDED(sq_getstring(v, topIdx + 1, &val))) {
          ss << GetSqObjTypeName(type) << (val ? val : "(anonymous)");

          // pop name of closure
          sq_poptop(v);
          break;
        }
      }
      ss << "Invalid Closure";
      break;
    }
    case OT_CLASS: {
      ss << GetSqObjTypeName(type) << " ";
      if (g_allowRecursion) ss << GetClassFullName(v);
      ss << " members: ";

      sq_pushnull(v);
      for (SQInteger i = 0; SQ_SUCCEEDED(sq_next(v, -2)); ++i) {
        if (i > 0) { ss << ", "; }

        //here -1 (aka top) is the value and -2 is the key

        sq_pop(v, 1);// pop val, so we can pretty print the key
        auto keyStr = prettyPrint(v);
        sq_pop(v, 1);// pop key before next iterations

        ss << keyStr;
      }
      sq_pop(v, 1);//pops the null iterator
      break;
    }
    case OT_INSTANCE: {
      if (SQ_SUCCEEDED(sq_getclass(v, topIdx))) {
        ss << "Instance of " << prettyPrint(v);
        sq_poptop(v);
      } else {
      }
      break;
    }
    case OT_ARRAY:
    case OT_TABLE: {
      const auto arrSize = sq_getsize(v, topIdx);
      const auto printElems = std::min(kMaxTablePrettyPrintSize, arrSize);

      //null iterator
      sq_pushnull(v);
      ss << GetSqObjTypeName(type);
      ss << " (len=" << arrSize << ") [";
      for (SQInteger i = 0; i < printElems && SQ_SUCCEEDED(sq_next(v, -2)); ++i) {
        if (i > 0) { ss << ", "; }

        //here -1 (aka top) is the value and -2 is the key

        auto valStr = g_allowRecursion ? prettyPrint(v) : "...";
        sq_pop(v, 1);// pop val, so we can pretty print the key
        auto keyStr = prettyPrint(v);
        sq_pop(v, 1);// pop key before next iteration

        ss << keyStr << ":" << valStr;
      }
      if (arrSize > printElems) { ss << ", ..."; }
      ss << "]";

      sq_pop(v, 1);//pops the null iterator
      break;
    }
    default:
      ss << GetSqObjTypeName(type);
  }

  return ss.str();
}

std::string GetClassFullName(HSQUIRRELVM v) {
  //g_allowRecursion = false;
  // TODO: Gonna need to cache this bad boy. Is this possible?

  if (sq_gettype(v, -1) != OT_CLASS) { throw std::runtime_error("Can't get the name of a class if it isn't a class!"); }

  auto findClassHash = sq_gethash(v, -1);
  std::unordered_map<SQHash, std::string> classNames;
  {
    sq_pushroottable(v);
    const std::string initialNamespace;
    GetClassesFullNameHelper(v, initialNamespace, classNames);
    sq_poptop(v);

    auto namePos = classNames.find(findClassHash);
    if (namePos != classNames.end()) {

      g_allowRecursion = true;
      return namePos->second;
    }
  }

  // Try looking up the local stacks
  for (SQInteger idx = sq_gettop(v); idx >= 0; idx--) {
    for (SQUnsignedInteger nseq = 0u;; ++nseq) {
      // Push local with given index to stack
      const auto localName = sq_getlocal(v, idx, nseq);
      if (localName == nullptr) { break; }

      const auto valType = sq_gettype(v, -1);
      if (valType == OT_TABLE) {
        const std::string initialNamespace;
        GetClassesFullNameHelper(v, initialNamespace, classNames);
      } else if (valType == OT_CLASS) {
        auto classHash = sq_gethash(v, -1);
        if (classHash == findClassHash) {
          // Remove local value from stack
          sq_poptop(v);

          return localName;
        }
      }

      // Remove local value from stack
      sq_poptop(v);
    }
  }

  throw std::runtime_error("Unknown class");
}

void SquirrelDebugger::SetEventInterface(std::shared_ptr<sdb::MessageEventInterface> eventInterface) {
  eventInterface_ = eventInterface;
}

void SquirrelDebugger::SetVm(HSQUIRRELVM vm) { vmData_.vm = vm; }

ReturnCode SquirrelDebugger::Pause() {
  if (pauseRequested_ == PauseType::None) {
    std::lock_guard lock(pauseMutex_);
    if (pauseRequested_ == PauseType::None) {
      pauseRequested_ = PauseType::Pause;
      pauseMutexData_.returnsRequired = -1;
    }
  }
  return ReturnCode::Success;
}

ReturnCode SquirrelDebugger::Continue() {
  if (pauseRequested_ != PauseType::None) {
    std::lock_guard lock(pauseMutex_);
    if (pauseRequested_ != PauseType::None) {
      pauseRequested_ = PauseType::None;
      pauseCv_.notify_all();
      return ReturnCode::Success;
    }
  }
  return ReturnCode::InvalidNotPaused;
}

ReturnCode SquirrelDebugger::StepOut() { return Step(PauseType::StepOut, 1); }

ReturnCode SquirrelDebugger::StepOver() { return Step(PauseType::StepOver, 0); }

ReturnCode SquirrelDebugger::StepIn() { return Step(PauseType::StepIn, -1); }

ReturnCode SquirrelDebugger::GetStackLocals(int32_t stackFrame, const std::string& path,
                                            std::vector<Variable>& variables) {
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_.isPaused) { return ReturnCode::InvalidNotPaused; }

  if (stackFrame > vmData_.currentStackDepth) { return ReturnCode::InvalidParameter; }
  vmData_.PopulateStackVariables(stackFrame, path, variables);

  return ReturnCode::Success;
}

ReturnCode SquirrelDebugger::Step(PauseType pauseType, int returnsRequired) {
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_.isPaused) { return ReturnCode::InvalidNotPaused; }

  pauseMutexData_.returnsRequired = returnsRequired;
  pauseRequested_ = pauseType;
  pauseCv_.notify_all();

  return ReturnCode::Success;
}

ReturnCode SquirrelDebugger::SendStatus() {
  // Don't allow unpause while we read the status.
  Status status;
  {
    std::lock_guard lock(pauseMutex_);
    if (pauseRequested_ != PauseType::None) {
      if (pauseMutexData_.isPaused) {
        // Make a copy of the last known status.
        status = pauseMutexData_.status;
        status.runstate = Runstate::Paused;
      } else if (pauseRequested_ == PauseType::Pause) {
        status.runstate = Runstate::Pausing;
      } else {
        status.runstate = Runstate::Stepping;
      }
    } else {
      status.runstate = Runstate::Running;
    }
  }

  eventInterface_->OnStatus(std::move(status));
  return ReturnCode::Success;
}

void SquirrelDebugger::SquirrelNativeDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line,
                                               const SQChar* funcname) {
  // 'c' called when a function has been called
  if (type == 'c') {
    ++vmData_.currentStackDepth;
    assert(vmData_.currentStackDepth < kMaxStackDepth);
    if (pauseRequested_ != PauseType::None) {
      std::unique_lock<std::mutex> lock(pauseMutex_);
      if (pauseRequested_ != PauseType::None) {
        if (pauseMutexData_.returnsRequired >= 0) { ++pauseMutexData_.returnsRequired; }
      }
    }
    // 'r' called when a function returns
  } else if (type == 'r') {
    --vmData_.currentStackDepth;
    assert(vmData_.currentStackDepth >= 0);
    if (pauseRequested_ != PauseType::None) {
      std::unique_lock<std::mutex> lock(pauseMutex_);
      if (pauseRequested_ != PauseType::None) { --pauseMutexData_.returnsRequired; }
    }
    // 'l' called every line(that contains some code)
  } else if (type == 'l') {
    if (pauseRequested_ != PauseType::None && pauseMutexData_.returnsRequired <= 0) {
      std::unique_lock<std::mutex> lock(pauseMutex_);
      if (pauseRequested_ != PauseType::None && pauseMutexData_.returnsRequired <= 0) {
        pauseMutexData_.isPaused = true;

        auto& status = pauseMutexData_.status;
        status.runstate = Runstate::Paused;

        vmData_.PopulateStack(status.stack);

        {
          Status statusCopy = status;
          eventInterface_->OnStatus(std::move(statusCopy));
        }

        // This Cv will be signaled whenever the value of pauseRequested_ changes.
        pauseCv_.wait(lock);
        pauseMutexData_.isPaused = false;
      }
    }

    return;
  }
}

void SquirrelDebugger::SquirrelVmData::PopulateStack(std::vector<sdb::data::StackEntry>& stack) const {
  stack.clear();

  SQStackInfos si;
  auto stackIdx = 0;
  while (SQ_SUCCEEDED(sq_stackinfos(vm, stackIdx, &si))) {
    stack.push_back({std::string(si.source), si.line, std::string(si.funcname)});
    ++stackIdx;
  }
}

void SquirrelDebugger::SquirrelVmData::PopulateStackVariables(int32_t stackFrame, const std::string& path,
                                                              std::vector<Variable>& stack) const {
  for (SQUnsignedInteger nseq = 0U;; ++nseq) {
    // Push local with given index to stack
    const auto* const localName = sq_getlocal(vm, stackFrame, nseq);
    if (localName == nullptr) { break; }

    Variable variable;
    variable.name = localName;
    variable.type = sdb_sq_typeof(vm);
    variable.value = sdb_sq_toString(vm);

    // Remove local from stack
    sq_poptop(vm);

    stack.emplace_back(std::move(variable));
  }
}
