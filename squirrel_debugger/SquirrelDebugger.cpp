#include "include/sdb/SquirrelDebugger.h"

#include <algorithm>
#include <squirrel.h>

#include <array>
#include <cassert>
#include <cstdarg>
#include <mutex>
#include <sstream>
#include <unordered_map>

// Quirrel Reference Guide: https://quirrel.io/doc/reference/embedding_squirrel.html

using sdb::SquirrelDebugger;
using sdb::data::PaginationInfo;
using sdb::data::ReturnCode;
using sdb::data::Runstate;
using sdb::data::StackEntry;
using sdb::data::Status;
using sdb::data::Variable;
using sdb::data::VariableType;

using LockGuard = std::lock_guard<std::recursive_mutex>;

const long long kMaxTablePrettyPrintSize = 5LL;
const uint32_t kMaxTableSizeToSort = 1000;
const uint32_t kMaxTableValueStringLength = 20;

const long long pooaxTablePrettyPrintSize = 5LL;
const long long ARC_BOAT = 5LL;
const long long ARC_BOAT2 = 5LL;

// Kinda just chosen arbitrarily - but if this isn't big enough, you should seriously consider changing your algorithm!
const long long kMaxStackDepth = 100;

const char* GetSqObjTypeName(SQObjectType sqType)
{
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
                              std::unordered_map<SQHash, std::string>& classNames)
{
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

VariableType sdb_sq_typeof(HSQUIRRELVM v)
{
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
}// Simple to_string of the var at the top of the stack.
// Useful for debugging.
std::string sdb_sq_toString(const HSQUIRRELVM v, const SQInteger idx)
{
  std::stringstream ss;
  switch (sq_gettype(v, idx)) {
    case OT_BOOL:
    {
      SQBool val;
      sq_getbool(v, idx, &val);
      ss << (val ? "true" : "false");
      break;
    }
    case OT_INTEGER:
    {
      SQInteger val;
      sq_getinteger(v, idx, &val);
      ss << val;
      break;
    }
    case OT_FLOAT:
    {
      SQFloat val;
      sq_getfloat(v, idx, &val);
      ss << val;
      break;
    }
    case OT_STRING:
    {
      const ::SQChar* val;
      sq_getstring(v, idx, &val);
      ss << '"' << val << '"';
      break;
    }
    default:
      return "";
  }
  return ss.str();
}

// Expects 2 things to be on the stack. -1=value, -2=key.
// Will pop both from the stack.
struct WriteTableSummaryFieldHelper {
  explicit WriteTableSummaryFieldHelper(const HSQUIRRELVM v, const bool isFirst)
      : vm(v)
      , isFirst(isFirst)
  {}
  const HSQUIRRELVM vm;
  const bool isFirst;
};
std::ostream& operator<<(std::ostream& ss, const WriteTableSummaryFieldHelper& helper)
{
  auto* const v = helper.vm;
  const auto valueStr = sdb_sq_toString(v, -1);
  if (!valueStr.empty()) {
    if (!helper.isFirst) { ss << ", "; }
    sq_poptop(v);// pop val, so we can get the key
    ss << sdb_sq_toString(v, -1) << ": " << valueStr;
    sq_poptop(v);// pop key
  } else {
    sq_pop(v, 2);
  }

  return ss;
}

ReturnCode CreateChildVariable(HSQUIRRELVM v, Variable& variable)
{
  const auto createTableSummary = [&](std::stringstream& ss) {
    ss << "{";
    // Table keys are not sorted alphabetically when iterating via sq_next.
    // If there aren't a large number of keys; get everything and perform a sort.
    const auto keyCount = sq_getsize(v, -1);
    variable.childCount = static_cast<uint32_t>(keyCount);
    if (keyCount < kMaxTableSizeToSort) {
      using KeyToTableIter = std::pair<std::string, SQInteger>;
      std::vector<KeyToTableIter> tableKeyToIterator;
      SQInteger sqIter = 0;
      sq_pushinteger(v, sqIter);
      for (SQInteger i = 0; SQ_SUCCEEDED(sq_getinteger(v, -1, &sqIter)) && SQ_SUCCEEDED(sq_next(v, -2)); ++i) {
        sq_poptop(v);// don't need the value.
        tableKeyToIterator.emplace_back(KeyToTableIter{sdb_sq_toString(v, -1), sqIter});
        sq_poptop(v);// pop key before next iteration
      }
      sq_poptop(v);
      std::sort(tableKeyToIterator.begin(), tableKeyToIterator.end(),
                [](const KeyToTableIter& lhs, const KeyToTableIter& rhs) -> bool { return lhs.first < rhs.first; });

      // Render summary of first few elements
      const auto initialSummarySize = ss.tellp();
      for (auto iter = tableKeyToIterator.begin();
           ss.tellp() - initialSummarySize < kMaxTableValueStringLength && iter != tableKeyToIterator.end(); ++iter) {
        sq_pushinteger(v, iter->second);
        if (!SQ_SUCCEEDED(sq_next(v, -2))) {
          sq_poptop(v);// pop iterator
          break;
        }
        ss << WriteTableSummaryFieldHelper(v, ss.tellp() == initialSummarySize);
        sq_poptop(v);// pop iterator
      }
    } else {
      // Render summary of first few elements
      sq_pushinteger(v, 0);
      for (SQInteger i = 0; ss.tellp() < kMaxTableValueStringLength && SQ_SUCCEEDED(sq_next(v, -2)); ++i) {
        ss << WriteTableSummaryFieldHelper(v, i == 0);
      }
      sq_poptop(v);
    }

    ss << "}";
  };

  std::stringstream ss;
  const auto topIdx = sq_gettop(v);
  const auto type = sq_gettype(v, topIdx);

  variable.type = VariableType::Other;
  variable.childCount = 0;
  switch (type) {
    case OT_BOOL:
    {
      variable.type = VariableType::Bool;
      SQBool val = SQFalse;
      if (SQ_SUCCEEDED(sq_getbool(v, topIdx, &val))) { ss << (val == SQTrue ? "true" : "false"); }
      break;
    }
    case OT_INTEGER:
    {
      variable.type = VariableType::Integer;
      SQInteger val = 0;
      if (SQ_SUCCEEDED(sq_getinteger(v, topIdx, &val))) { ss << val; }
      break;
    }
    case OT_FLOAT:
    {
      variable.type = VariableType::Float;
      SQFloat val = 0.0f;
      if (SQ_SUCCEEDED(sq_getfloat(v, topIdx, &val))) { ss << val; }
      break;
    }
    case OT_STRING:
    {
      variable.type = VariableType::String;
      const ::SQChar* val = nullptr;
      if (SQ_SUCCEEDED(sq_getstring(v, topIdx, &val))) { ss << val; }
      break;
    }
    case OT_CLOSURE:
    {
      variable.type = VariableType::Closure;
      if (SQ_SUCCEEDED(sq_getclosurename(v, -1))) {
        const ::SQChar* val = nullptr;
        if (SQ_SUCCEEDED(sq_getstring(v, -1, &val))) {
          ss << (val != nullptr ? val : "(anonymous)");

          // pop name of closure
          sq_poptop(v);
        }
      } else {
        ss << "Invalid Closure";
      }

      SQInteger nparams;
      SQInteger nfreevars;
      if (SQ_SUCCEEDED(sq_getclosureinfo(v, -1, &nparams, &nfreevars))) {
        ss << "(";
        ss << nparams << " params, " << nfreevars << " freevars";
        ss << ")";
      }
      break;
    }
    case OT_CLASS:
    {
      variable.type = VariableType::Class;
      ss << GetClassFullName(v);
      break;
    }
    case OT_ARRAY:
    {
      variable.type = VariableType::Array;
      const auto arrSize = sq_getsize(v, topIdx);
      variable.childCount = static_cast<uint32_t>(arrSize);

      // Add a suffix to the summary
      ss << "{ size=" << arrSize << " }";
    } break;
    case OT_INSTANCE:
    {
      variable.type = VariableType::Instance;
      if (SQ_SUCCEEDED(sq_getclass(v, topIdx))) {
        ss << GetClassFullName(v) << " ";
        sq_poptop(v);// pop class
      }
      createTableSummary(ss);
    } break;
    case OT_TABLE:
    {
      variable.type = VariableType::Table;
      createTableSummary(ss);
    } break;
    default:
      ss << GetSqObjTypeName(type);
  }

  variable.value = ss.str();
  return ReturnCode::Success;
}

ReturnCode CreateChildVariables(HSQUIRRELVM v, const PaginationInfo& pagination, std::vector<Variable>& variables)
{
  const auto createTableChildVariableFromIter = [vm = v](Variable& variable) -> ReturnCode {
    const auto retVal = CreateChildVariable(vm, variable);
    if (ReturnCode::Success != retVal) {
      sq_pop(vm, 2);
      return retVal;
    }

    sq_poptop(vm);// pop val, so we can get the key
    variable.name = sdb_sq_toString(vm, -1);
    sq_poptop(vm);// pop key before next iteration

    return ReturnCode::Success;
  };

  const auto type = sq_gettype(v, -1);
  switch (type) {
    case OT_ARRAY:
    {
      const auto arrSize = sq_getsize(v, -1);
      sq_pushinteger(v, pagination.beginIndex);
      for (SQInteger i = 0; i < pagination.count && SQ_SUCCEEDED(sq_next(v, -2)); ++i) {
        Variable childVar = {};
        std::stringstream childName;
        childName << i;
        childVar.name = childName.str();

        CreateChildVariable(v, childVar);

        sq_poptop(v);// pop val, so we can get the key
        childVar.name = sdb_sq_toString(v, -1);
        sq_poptop(v);// pop key before next iteration

        variables.emplace_back(std::move(childVar));
      }
      sq_poptop(v);
    } break;
    case OT_INSTANCE:
    case OT_TABLE:
    {

      // Table keys are not sorted alphabetically when iterating via sq_next.
      // If there aren't a large number of keys; get everything and perform a sort.
      const auto keyCount = sq_getsize(v, -1);
      if (keyCount < kMaxTableSizeToSort) {
        using KeyToTableIter = std::pair<std::string, SQInteger>;
        std::vector<KeyToTableIter> tableKeyToIterator;
        SQInteger sqIter = 0;
        sq_pushinteger(v, sqIter);
        for (SQInteger i = 0; SQ_SUCCEEDED(sq_getinteger(v, -1, &sqIter)) && SQ_SUCCEEDED(sq_next(v, -2)); ++i) {
          sq_poptop(v);// don't need the value.
          tableKeyToIterator.emplace_back(KeyToTableIter{sdb_sq_toString(v, -1), sqIter});
          sq_poptop(v);// pop key before next iteration
        }
        sq_poptop(v);
        std::sort(tableKeyToIterator.begin(), tableKeyToIterator.end(),
                  [](const KeyToTableIter& lhs, const KeyToTableIter& rhs) -> bool { return lhs.first < rhs.first; });

        // Now add children
        auto childKeyIter = tableKeyToIterator.begin() + pagination.beginIndex;
        for (uint32_t i = 0u; i < pagination.count && childKeyIter != tableKeyToIterator.end(); ++i) {
          sq_pushinteger(v, childKeyIter->second);
          ++childKeyIter;
          if (!SQ_SUCCEEDED(sq_next(v, -2))) {
            sq_poptop(v);// pop iterator
            break;
          }
          Variable variable;
          const auto retVal = createTableChildVariableFromIter(variable);
          if (ReturnCode::Success != retVal) {
            sq_poptop(v);// pop iterator
            return retVal;
          }
          variables.emplace_back(std::move(variable));
          sq_poptop(v);// pop iterator
        }
      } else {
        // Now add children
        sq_pushinteger(v, pagination.beginIndex);
        for (SQInteger i = 0; i < pagination.count && SQ_SUCCEEDED(sq_next(v, -2)); ++i) {
          Variable variable;
          const auto retVal = createTableChildVariableFromIter(variable);
          if (ReturnCode::Success != retVal) {
            sq_poptop(v);// pop iterator
            return retVal;
          }
          variables.emplace_back(std::move(variable));
        }
        // pop iterator
        sq_poptop(v);
      }
    }
  }
  return ReturnCode::Success;
}

ReturnCode sdb_sq_readVariableChildren(HSQUIRRELVM v, std::vector<std::string>::const_iterator pathBegin,
                               std::vector<std::string>::const_iterator pathEnd, const PaginationInfo& pagination,
                               std::vector<Variable>& variables)
{
  if (pathBegin == pathEnd)
  {
    // Add the children of the variable at the top of the stack to the list.
    return CreateChildVariables(v, pagination, variables);
  }

  std::stringstream ss;
  const auto topIdx = sq_gettop(v);
  const auto type = sq_gettype(v, topIdx);

  // Push the indexed child on to the stack
  switch (type) {
    case OT_ARRAY:
    {
      const auto arrSize = sq_getsize(v, topIdx);
      std::stringstream indexStr(*pathBegin);
      int arrIndex = 0;
      indexStr >> arrIndex;
      if (indexStr.fail()) {
        // TODO: Log where in the path it went wrong
        return ReturnCode::InvalidParameter;
      }
      if (arrIndex >= arrSize) {
        // TODO: Log where in the path it went wrong
        return ReturnCode::InvalidParameter;
      }

      sq_pushinteger(v, arrIndex);
      const auto sqRetVal = sq_get(v, -2);
      if (!SQ_SUCCEEDED(sqRetVal)) {
        // TODO: Log where in the path it went wrong
        return ReturnCode::InvalidParameter;
      }

    } break;
    case OT_TABLE:
    case OT_INSTANCE:
    case OT_CLOSURE:
    {
      const auto& key = *pathBegin;
      sq_pushstring(v, key.c_str(), key.size());
      if (!SQ_SUCCEEDED(sq_get(v, -2))) {
        // TODO: Log where in the path it went wrong
        return ReturnCode::InvalidParameter;
      }

    } break;
    default:
      return ReturnCode::InvalidParameter;
  }
  
  const auto childRetVal = sdb_sq_readVariableChildren(v, pathBegin + 1, pathEnd, pagination, variables);

  // Pop the indexed object from the stack
  sq_poptop(v);
  return childRetVal;

  //auto readArray = [&ss, v](const SQObjectType type, const SQInteger topIdx) {
  //  const auto arrSize = sq_getsize(v, topIdx);

  //  //null iterator
  //  sq_pushnull(v);
  //  ss << GetSqObjTypeName(type);
  //  ss << " (len=" << arrSize << ")";

  //  for (SQInteger i = 0; i < printElems && SQ_SUCCEEDED(sq_next(v, -2)); ++i) {
  //    if (i > 0) { ss << ", "; }

  //    //here -1 (aka top) is the value and -2 is the key

  //    auto valStr = g_allowRecursion ? prettyPrint(v) : "...";
  //    sq_pop(v, 1);// pop val, so we can pretty print the key
  //    auto keyStr = prettyPrint(v);
  //    sq_pop(v, 1);// pop key before next iteration

  //    ss << keyStr << ":" << valStr;
  //  }
  //  if (arrSize > printElems) { ss << ", ..."; }
  //  ss << "]";

  //  sq_pop(v, 1);//pops the null iterator
  //};
}


// Simple to_string of the var at the top of the stack.
// Useful for debugging.
std::string prettyPrint(HSQUIRRELVM v)
{
  std::stringstream ss;
  const auto topIdx = sq_gettop(v);
  const auto type = sq_gettype(v, topIdx);
  switch (type) {
    case OT_BOOL:
    {
      SQBool val;
      sq_getbool(v, topIdx, &val);
      ss << (val ? "true" : "false");
      break;
    }
    case OT_INTEGER:
    {
      SQInteger val;
      sq_getinteger(v, topIdx, &val);
      ss << val;
      break;
    }
    case OT_FLOAT:
    {
      SQFloat val;
      sq_getfloat(v, topIdx, &val);
      ss << val;
      break;
    }
    case OT_STRING:
    {
      const ::SQChar* val;
      sq_getstring(v, topIdx, &val);
      ss << '"' << val << '"';
      break;
    }
    case OT_CLOSURE:
    {
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
    case OT_CLASS:
    {
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
    case OT_INSTANCE:
    {
      if (SQ_SUCCEEDED(sq_getclass(v, topIdx))) {
        ss << "Instance of " << prettyPrint(v);
        sq_poptop(v);
      } else {
      }
      break;
    }
    case OT_ARRAY:
    case OT_TABLE:
    {
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

std::string GetClassFullName(HSQUIRRELVM v)
{
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

void SquirrelDebugger::SetEventInterface(std::shared_ptr<sdb::MessageEventInterface> eventInterface)
{
  eventInterface_ = eventInterface;
}

void SquirrelDebugger::SetVm(HSQUIRRELVM vm) { vmData_.vm = vm; }

ReturnCode SquirrelDebugger::Pause()
{
  if (pauseRequested_ == PauseType::None) {
    std::lock_guard lock(pauseMutex_);
    if (pauseRequested_ == PauseType::None) {
      pauseRequested_ = PauseType::Pause;
      pauseMutexData_.returnsRequired = -1;
    }
  }
  return ReturnCode::Success;
}

ReturnCode SquirrelDebugger::Continue()
{
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
                                            const data::PaginationInfo& pagination, std::vector<Variable>& variables)
{
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_.isPaused) { return ReturnCode::InvalidNotPaused; }

  if (stackFrame > vmData_.currentStackDepth) { return ReturnCode::InvalidParameter; }
  return vmData_.PopulateStackVariables(stackFrame, path, pagination, variables);
}

ReturnCode SquirrelDebugger::Step(PauseType pauseType, int returnsRequired)
{
  std::lock_guard lock(pauseMutex_);
  if (!pauseMutexData_.isPaused) { return ReturnCode::InvalidNotPaused; }

  pauseMutexData_.returnsRequired = returnsRequired;
  pauseRequested_ = pauseType;
  pauseCv_.notify_all();

  return ReturnCode::Success;
}

ReturnCode SquirrelDebugger::SendStatus()
{
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
                                               const SQChar* funcname)
{
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
    if (line == 43 && pauseRequested_ == PauseType::None) { pauseRequested_ = PauseType::Pause; }

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

void SquirrelDebugger::SquirrelVmData::PopulateStack(std::vector<StackEntry>& stack) const
{
  stack.clear();

  SQStackInfos si;
  auto stackIdx = 0;
  while (SQ_SUCCEEDED(sq_stackinfos(vm, stackIdx, &si))) {
    stack.push_back({std::string(si.source), si.line, std::string(si.funcname)});
    ++stackIdx;
  }
}

ReturnCode SquirrelDebugger::SquirrelVmData::PopulateStackVariables(int32_t stackFrame, const std::string& path,
                                                                    const data::PaginationInfo& pagination,
                                                                    std::vector<Variable>& stack) const
{
  std::vector<std::string> pathParts;
  if (!path.empty()) {
    // Convert comma-separated list to vector
    std::stringstream s_stream(path);
    while (s_stream.good()) {
      std::string substr;
      getline(s_stream, substr, '.');
      pathParts.emplace_back(std::move(substr));
    }
  }

  ReturnCode rc = ReturnCode::Success;
  const auto maxNSeq = pagination.beginIndex + pagination.count;
  if (pathParts.begin() == pathParts.end()) {
    for (SQUnsignedInteger nSeq = pagination.beginIndex; nSeq < maxNSeq; ++nSeq) {
      // Push local with given index to stack
      const auto* const localName = sq_getlocal(vm, stackFrame, nSeq);
      if (localName == nullptr) { break; }

      Variable variable;
      variable.name = localName;
      rc = CreateChildVariable(vm, variable);
      if (rc != ReturnCode::Success) { break; }

      // Remove local from stack
      sq_poptop(vm);

      stack.emplace_back(std::move(variable));
    }
  } else {
    for (SQUnsignedInteger nSeq = pagination.beginIndex; nSeq < maxNSeq; ++nSeq) {
      // Push local with given index to stack
      const auto* const localName = sq_getlocal(vm, stackFrame, nSeq);
      if (localName == nullptr) { break; }

      // make sure the root stack variable matches.
      if (*pathParts.begin() != localName) { continue; }

      //variable.name = *(pathParts.end() - 1);
      rc = sdb_sq_readVariableChildren(vm, pathParts.begin() + 1, pathParts.end(), pagination, stack);
      if (rc != ReturnCode::Success) { break; }

      // Remove local from stack
      sq_poptop(vm);
    }
  }

  return rc;
}
