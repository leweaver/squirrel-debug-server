#include "SquirrelVmHelpers.h"

#include <sdb/LogInterface.h>
#include <sdb/MessageInterface.h>


#include <algorithm>
#include <array>
#include <mutex>
#include <sstream>
#include <unordered_map>

const uint32_t kMaxTableSizeToSort = 1000;
const uint32_t kMaxTableValueStringLength = 20;

// Quirrel Reference Guide: https://quirrel.io/doc/reference/embedding_squirrel.html

using sdb::data::PaginationInfo;
using sdb::data::ReturnCode;
using sdb::data::Variable;
using sdb::data::VariableType;

namespace sdb::sq {

const std::array<const char*, 18> kTypeNames = {
        "NULL",          "INTEGER",   "FLOAT",       "BOOL",   "STRING",    "TABLE", "ARRAY",    "USERDATA", "CLOSURE",
        "NATIVECLOSURE", "GENERATOR", "USERPOINTER", "THREAD", "FUNCPROTO", "CLASS", "INSTANCE", "WEAKREF",  "OUTER",
};
const std::array<VariableType, 18> kVariableTypes = {
        VariableType::Null,    VariableType::Integer,       VariableType::Float,     VariableType::Bool,
        VariableType::String,  VariableType::Table,         VariableType::Array,     VariableType::UserData,
        VariableType::Closure, VariableType::NativeClosure, VariableType::Generator, VariableType::UserPointer,
        VariableType::Thread,  VariableType::FuncProto,     VariableType::Class,     VariableType::Instance,
        VariableType::WeakRef, VariableType::Outer,
};

const char* toSqObjectTypeName(SQObjectType sqType)
{
  // Get index of least sig set bit:
#ifdef _MSC_VER
  const unsigned idx = __lzcnt(_RAW_TYPE(static_cast<unsigned>(sqType)));
#else
  const auto idx = __builtin_ffs(_RAW_TYPE(sqType));
#endif
  return kTypeNames.at(31 - idx);
}

VariableType toVariableType(const SQObjectType sqType)
{
  // Get index of least sig set bit:
#ifdef _MSC_VER
  const unsigned idx = __lzcnt(_RAW_TYPE(static_cast<unsigned>(sqType)));
#else
  const auto idx = __builtin_ffs(_RAW_TYPE(sqType));
#endif
  return kVariableTypes.at(31 - idx);
}


// Expects 2 things to be on the stack. -1=value, -2=key.
// Will pop both from the stack.
struct WriteTableSummaryFieldHelper {
  explicit WriteTableSummaryFieldHelper(SQVM* const v, const bool isFirst)
      : vm(v)
      , isFirst(isFirst)
  {}
  SQVM* const vm;
  const bool isFirst;
};
std::ostream& operator<<(std::ostream& ss, const WriteTableSummaryFieldHelper& helper)
{
  auto* const v = helper.vm;
  const auto valueStr = toString(v, -1);
  if (!valueStr.empty()) {
    if (!helper.isFirst) {
      ss << ", ";
    }
    sq_poptop(v);// pop val, so we can get the key
    ss << toString(v, -1) << ": " << valueStr;
    sq_poptop(v);// pop key
  }
  else {
    sq_pop(v, 2);
  }

  return ss;
}

void getClassesFullNameHelper(
        HSQUIRRELVM v, const std::string& currentNamespace, std::unordered_map<SQHash, std::string>& classNames)
{
  if (sq_gettype(v, -1) != OT_TABLE) {
    throw std::runtime_error("Must have a table at the top of the stack.");
  }

  sq_pushnull(v);

  // Iterate over the table.
  while (SQ_SUCCEEDED(sq_next(v, -2))) {
    // What's the type of the VALUE?
    const auto type = sq_gettype(v, -1);
    if (type == OT_TABLE || type == OT_CLASS) {
      const ::SQChar* key;
      sq_getstring(v, -2, &key);
      auto newNamespace = currentNamespace;
      if (!currentNamespace.empty()) {
        newNamespace.append(".");
      }
      newNamespace.append(key);

      if (type == OT_CLASS) {
        const auto classHash = sq_gethash(v, -1);
        if (classNames.find(classHash) != classNames.end()) {
          throw std::runtime_error("class already added man");
        }
        classNames[classHash] = newNamespace;
      }
      else {
        getClassesFullNameHelper(v, newNamespace, classNames);
      }
    }
    sq_pop(v, 2);
  }

  sq_pop(v, 1);//pops the null iterator
}

void CreateTableSummary(HSQUIRRELVM v, std::stringstream& ss)
{
  ss << "{";
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
      tableKeyToIterator.emplace_back(KeyToTableIter{toString(v, -1), sqIter});
      sq_poptop(v);// pop key before next iteration
    }
    sq_poptop(v);
    std::sort(
            tableKeyToIterator.begin(), tableKeyToIterator.end(),
            [](const KeyToTableIter& lhs, const KeyToTableIter& rhs) -> bool { return lhs.first < rhs.first; });

    // Render summary of first few elements
    const auto initialSummarySize = ss.tellp();
    for (auto iter = tableKeyToIterator.begin();
         ss.tellp() - initialSummarySize < kMaxTableValueStringLength && iter != tableKeyToIterator.end(); ++iter)
    {
      sq_pushinteger(v, iter->second);
      if (!SQ_SUCCEEDED(sq_next(v, -2))) {
        sq_poptop(v);// pop iterator
        break;
      }
      ss << WriteTableSummaryFieldHelper(v, ss.tellp() == initialSummarySize);
      sq_poptop(v);// pop iterator
    }
  }
  else {
    // Render summary of first few elements
    sq_pushinteger(v, 0);
    for (SQInteger i = 0; ss.tellp() < kMaxTableValueStringLength && SQ_SUCCEEDED(sq_next(v, -2)); ++i) {
      ss << WriteTableSummaryFieldHelper(v, i == 0);
    }
    sq_poptop(v);
  }

  ss << "}";
};

// Simple to_string of the var at the top of the stack.
std::string toString(SQVM* const v, const SQInteger idx)
{
  std::stringstream ss;
  const auto type = sq_gettype(v, idx);
  switch (type) {
    case OT_BOOL:
    {
      SQBool val = SQFalse;
      if (SQ_SUCCEEDED(sq_getbool(v, idx, &val))) {
        ss << (val == SQTrue ? "true" : "false");
      }
      break;
    }
    case OT_INTEGER:
    {
      SQInteger val = 0;
      if (SQ_SUCCEEDED(sq_getinteger(v, idx, &val))) {
        ss << val;
      }
      break;
    }
    case OT_FLOAT:
    {
      SQFloat val = 0.0F;
      if (SQ_SUCCEEDED(sq_getfloat(v, idx, &val))) {
        ss << val;
      }
      break;
    }
    case OT_STRING:
    {
      const ::SQChar* val = nullptr;
      if (SQ_SUCCEEDED(sq_getstring(v, idx, &val))) {
        ss << val;
      }
      break;
    }
    case OT_CLOSURE:
    {
      if (SQ_SUCCEEDED(sq_getclosurename(v, idx))) {
        const ::SQChar* val = nullptr;
        if (SQ_SUCCEEDED(sq_getstring(v, -1, &val))) {
          ss << (val != nullptr ? val : "(anonymous)");

          // pop name of closure
          sq_poptop(v);
        }
      }
      else {
        ss << "Invalid Closure";
      }

      SQInteger numParams = 0;
      SQInteger numFreeVars = 0;
      if (SQ_SUCCEEDED(sq_getclosureinfo(v, idx, &numParams, &numFreeVars))) {
        ss << "(";
        ss << numParams << " params, " << numFreeVars << " freeVars";
        ss << ")";
      }
      break;
    }
    case OT_CLASS:
    {
      ss << classFullName(v, -1);
      break;
    }
    case OT_ARRAY:
    {
      const auto arrSize = sq_getsize(v, idx);

      // Add a suffix to the summary
      ss << "{ size=" << arrSize << " }";
    } break;
    case OT_INSTANCE:
    case OT_TABLE:
    {
      CreateTableSummary(v, ss);
    } break;
    default:
      ss << toSqObjectTypeName(type);
  }

  return ss.str();
}

ReturnCode createChildVariable(SQVM* const v, Variable& variable)
{
  std::stringstream ss;
  const auto topIdx = sq_gettop(v);
  const auto type = sq_gettype(v, topIdx);

  variable.valueRawAddress = 0;
  if (ISREFCOUNTED(type)) {
    HSQOBJECT stackObj;
    if (SQ_SUCCEEDED(sq_getstackobj(v, -1, &stackObj))) {
      variable.valueRawAddress = stackObj._unVal.raw;
    }
  }

  variable.valueType = toVariableType(sq_gettype(v, -1));
  variable.value = toString(v, -1);

  switch (variable.valueType) {
    case VariableType::Instance:
    {
      if (SQ_SUCCEEDED(sq_getclass(v, -1))) {
        variable.instanceClassName = classFullName(v, -1);
        sq_poptop(v);// pop class
      }
      else {
        SDB_LOGD(__FILE__, "Failed to find classname");
      }
      [[fallthrough]];
    }
    case VariableType::Array:
    case VariableType::Table:
      variable.childCount = static_cast<uint32_t>(sq_getsize(v, -1));
      break;
    default:
      variable.childCount = 0;
  }

  return ReturnCode::Success;
}

ReturnCode CreateChildVariables(SQVM* const v, const PaginationInfo& pagination, std::vector<Variable>& variables)
{
  const auto createTableChildVariableFromIter = [vm = v](Variable& variable) -> ReturnCode {
    const auto retVal = createChildVariable(vm, variable);
    if (ReturnCode::Success != retVal) {
      sq_pop(vm, 2);
      return retVal;
    }

    sq_poptop(vm);// pop val, so we can get the key
    variable.pathUiString = toString(vm, -1);
    variable.pathTableKeyType = toVariableType(sq_gettype(vm, -1));
    sq_poptop(vm);// pop key before next iteration

    return ReturnCode::Success;
  };

  switch (sq_gettype(v, -1)) {
    case OT_ARRAY:
    {
      SQInteger sqIter = pagination.beginIterator;
      sq_pushinteger(v, sqIter);
      for (SQInteger i = 0;
           i < pagination.count && SQ_SUCCEEDED(sq_getinteger(v, -1, &sqIter)) && SQ_SUCCEEDED(sq_next(v, -2)); ++i)
      {
        Variable childVar = {};

        createChildVariable(v, childVar);

        sq_poptop(v);// pop val, so we can get the key
        childVar.pathIterator = sqIter;
        childVar.pathUiString = toString(v, -1);
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
          tableKeyToIterator.emplace_back(KeyToTableIter{toString(v, -1), sqIter});
          sq_poptop(v);// pop key before next iteration
        }
        sq_poptop(v);
        std::sort(
                tableKeyToIterator.begin(), tableKeyToIterator.end(),
                [](const KeyToTableIter& lhs, const KeyToTableIter& rhs) -> bool { return lhs.first < rhs.first; });

        // Now add children
        auto childKeyIter = tableKeyToIterator.begin() + pagination.beginIterator;
        for (uint32_t i = 0U; i < pagination.count && childKeyIter != tableKeyToIterator.end(); ++i, ++childKeyIter) {
          sq_pushinteger(v, childKeyIter->second);
          if (!SQ_SUCCEEDED(sq_next(v, -2))) {
            sq_poptop(v);// pop iterator
            break;
          }
          Variable variable;
          variable.pathIterator = childKeyIter->second;
          const auto retVal = createTableChildVariableFromIter(variable);
          if (ReturnCode::Success != retVal) {
            sq_poptop(v);// pop iterator
            return retVal;
          }
          variables.emplace_back(std::move(variable));
          sq_poptop(v);// pop iterator
        }
      }
      else {
        // Now add children
        sq_pushinteger(v, pagination.beginIterator);
        SQInteger sqIter = 0;
        for (SQInteger i = 0;
             i < pagination.count && SQ_SUCCEEDED(sq_getinteger(v, -1, &sqIter)) && SQ_SUCCEEDED(sq_next(v, -2)); ++i)
        {
          Variable variable;
          variable.pathIterator = sqIter;
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
    } break;
    default:
      break;
  }
  return ReturnCode::Success;
}

ReturnCode createChildVariablesFromIterable(
        SQVM* const v, const std::vector<uint64_t>::const_iterator pathBegin,
        const std::vector<uint64_t>::const_iterator pathEnd, const PaginationInfo& pagination,
        std::vector<Variable>& variables)
{
  ScopedVerifySqTop scopedVerify(v);

  if (pathBegin == pathEnd) {
    // Add the children of the variable at the top of the stack to the list.
    return CreateChildVariables(v, pagination, variables);
  }

  std::stringstream ss;

  // Push the indexed child on to the stack
  const auto type = sq_gettype(v, -1);
  switch (type) {
    case OT_ARRAY:
    {
      const auto arrSize = sq_getsize(v, -1);
      const int arrIndex = static_cast<int>(*pathBegin);
      if (arrIndex >= arrSize) {
        SDB_LOGD(__FILE__, "Array index %d out of bounds", *pathBegin);
        return ReturnCode::InvalidParameter;
      }

      sq_pushinteger(v, arrIndex);
      const auto sqRetVal = sq_get(v, -2);
      if (!SQ_SUCCEEDED(sqRetVal)) {
        SDB_LOGD(__FILE__, "Failed to get array index %d", arrIndex);
        return ReturnCode::InvalidParameter;
      }
      const auto childRetVal = createChildVariablesFromIterable(v, pathBegin + 1, pathEnd, pagination, variables);
      sq_poptop(v);// pop value
      return childRetVal;
    }
    case OT_TABLE:
    case OT_INSTANCE:
    case OT_CLOSURE:
    {
      sq_pushinteger(v, *pathBegin);
      if (!SQ_SUCCEEDED(sq_next(v, -2))) {
        SDB_LOGD(__FILE__, "Failed to read iterator %d", *pathBegin);
        sq_poptop(v);// pop iterator
        return ReturnCode::InvalidParameter;
      }
      const auto childRetVal = createChildVariablesFromIterable(v, pathBegin + 1, pathEnd, pagination, variables);
      sq_pop(v, 3);// pop value, key and iterator
      return childRetVal;
    }
    default:
      SDB_LOGD(__FILE__, "Iterator points to non iterable type: %d", type);
      return ReturnCode::InvalidParameter;
  }
}

std::string classFullName(SQVM* const v, const SQInteger idx)
{
  // TODO: May want to cache this bad boy. Is this even possible?

  if (sq_gettype(v, -1) != OT_CLASS) {
    throw std::runtime_error("Can't get the name of a class if it isn't a class!");
  }

  const auto findClassHash = sq_gethash(v, idx);
  std::unordered_map<SQHash, std::string> classNames;
  {
    sq_pushroottable(v);
    const std::string initialNamespace;
    getClassesFullNameHelper(v, initialNamespace, classNames);
    sq_poptop(v);

    const auto namePos = classNames.find(findClassHash);
    if (namePos != classNames.end()) {
      return namePos->second;
    }
  }

  // Try looking up the local stacks
  SQStackInfos si;
  SQInteger stackIdx = 0;
  while (SQ_SUCCEEDED(sq_stackinfos(v, stackIdx, &si))) {
    for (SQUnsignedInteger nSeq = 0U;; ++nSeq) {
      // Push local with given index to stack
      const auto* const localName = sq_getlocal(v, stackIdx, nSeq);
      if (localName == nullptr) {
        break;
      }

      const auto valType = sq_gettype(v, -1);
      if (valType == OT_TABLE) {
        const std::string initialNamespace;
        getClassesFullNameHelper(v, initialNamespace, classNames);
      }
      else if (valType == OT_CLASS) {
        const auto classHash = sq_gethash(v, -1);
        if (classHash == findClassHash) {
          // Remove local value from stack
          sq_poptop(v);

          return localName;
        }
      }

      // Remove local value from stack
      sq_poptop(v);
    }

    ++stackIdx;
  }

  throw std::runtime_error("Unknown class");
}
}// namespace sdb::sq