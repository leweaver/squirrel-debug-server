#include "SquirrelVmHelpers.h"

#include <sdb/LogInterface.h>
#include <sdb/MessageInterface.h>


#include <algorithm>
#include <array>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <functional>

const char* const kLogTag = "SquirrelVmHelpers";

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

const char* ToSqObjectTypeName(SQObjectType sqType)
{
  // Get index of least sig set bit:
#ifdef _MSC_VER
  const unsigned idx = __lzcnt(_RAW_TYPE(static_cast<unsigned>(sqType)));
#else
  const auto idx = __builtin_ffs(_RAW_TYPE(sqType));
#endif
  return kTypeNames.at(31 - idx);
}

VariableType ToVariableType(const SQObjectType sqType)
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
  const auto valueStr = ToString(v, -1);
  if (!valueStr.empty()) {
    if (!helper.isFirst) {
      ss << ", ";
    }
    sq_poptop(v);// pop val, so we can get the key
    ss << ToString(v, -1) << ": " << valueStr;
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

  ScopedVerifySqTop scopedVerify(v);
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
          sq_pop(v, 2);// pop iterator and key/value

          // class already added - this table may have a reference to itself.
          break;
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
      tableKeyToIterator.emplace_back(KeyToTableIter{ToString(v, -1), sqIter});
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

ReturnCode UpdateFromString(SQVM* const v, SQInteger objIdx, const std::string& value)
{
   // make into absolute positions
   const auto type = sq_gettype(v, -1);

   // push the new value
   switch (type) {
     case OT_BOOL:
     {
       SQBool val = value == "true" || value == "1" ? SQTrue : SQFalse;
       sq_poptop(v);
       sq_pushbool(v, val);
       break;
     }
     case OT_INTEGER:
     {
       SQInteger newVal;
       try {
         newVal = std::stoi(value);
       }
       catch (const std::logic_error& e) {
         SDB_LOGE(kLogTag, "UpdateFromString: failed to parse int from %s (%s)", value.c_str(), e.what());
         return ReturnCode::InvalidParameter;
       }
       sq_poptop(v);
       sq_pushinteger(v, newVal);
       break;
     }
     case OT_FLOAT:
     {
       float newVal;
       try {
         newVal= std::stof(value);
       }
       catch (const std::logic_error& e) {
         SDB_LOGE(kLogTag, "UpdateFromString: failed to parse float from %s (%s)", value.c_str(), e.what());
         return ReturnCode::InvalidParameter;
       }
       sq_poptop(v);
       sq_pushfloat(v, newVal);
       break;
     }
     case OT_STRING:
     {
       sq_poptop(v);
       sq_pushstring(v, value.c_str(), SQInteger(value.size()));
       break;
     }
     default:
       SDB_LOGE(kLogTag, "UpdateFromString: Unsupported variable type");
       return ReturnCode::InvalidParameter;
   }

   if (SQ_SUCCEEDED(sq_set(v, objIdx)))
   {
     return ReturnCode::Success;
   }
   else
   {
     SDB_LOGE(kLogTag, "UpdateFromString: Failed to set value due to unknown error");
     return ReturnCode::ErrorInternal;
   }
}

// Simple to_string of the var at the top of the stack.
std::string ToString(SQVM* const v, const SQInteger idx)
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
      ss << ToClassFullName(v, -1);
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
      ss << ToSqObjectTypeName(type);
  }

  return ss.str();
}

ReturnCode CreateChildVariable(SQVM* const v, Variable& variable)
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

  variable.valueType = ToVariableType(sq_gettype(v, -1));
  variable.value = ToString(v, -1);

  switch (variable.valueType) {
    case VariableType::Instance:
    {
      if (SQ_SUCCEEDED(sq_getclass(v, -1))) {
        variable.instanceClassName = ToClassFullName(v, -1);
        sq_poptop(v);// pop class
      }
      else {
        SDB_LOGD(kLogTag, "Failed to find classname");
      }

      [[fallthrough]];
    }
    // Case "Instance" falls through to here as well.
    case VariableType::UserData:
    {
      // Read the count from the 'delegate', which is a table containing the fields and properties.
      HSQOBJECT sqObj{};
      variable.childCount = 0;
      if (SQ_SUCCEEDED(sq_getstackobj(v, -1, &sqObj))) {
        // There's an issue in the squirrel implementation of `sq_getdelegate` - it doesn't handle Instances.
        // Hack the type to make it a table to trick the API (this doesn't change the underlying type)
        sqObj._type = OT_TABLE;
        sq_pushobject(v, sqObj);
        if (SQ_SUCCEEDED(sq_getdelegate(v, -1))) {
          variable.childCount = static_cast<uint32_t>(sq_getsize(v, -1));
          sq_poptop(v);
        }
        else {
          SDB_LOGD(kLogTag, "Failed to get delegate");
        }
        sq_poptop(v);
      }
      break;
    }
    case VariableType::Array:
    case VariableType::Table:
      variable.childCount = static_cast<uint32_t>(sq_getsize(v, -1));
      break;
    default:
      variable.childCount = 0;
  }

  if (variable.valueType == VariableType::Bool || variable.valueType == VariableType::Float ||
      variable.valueType == VariableType::Integer || variable.valueType == VariableType::String)
  {
    variable.editable = true;
  }
  else {
    variable.editable = false;
  }

  return ReturnCode::Success;
}

ReturnCode CreateChildVariables(SQVM* const v, const PaginationInfo& pagination, std::vector<Variable>& variables)
{
  const auto createTableChildVariableFromIter = [vm = v](Variable& variable) -> ReturnCode {
    const auto retVal = CreateChildVariable(vm, variable);
    if (ReturnCode::Success != retVal) {
      sq_pop(vm, 2);
      return retVal;
    }

    sq_poptop(vm);// pop val, so we can get the key
    variable.pathUiString = ToString(vm, -1);
    variable.pathTableKeyType = ToVariableType(sq_gettype(vm, -1));
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

        CreateChildVariable(v, childVar);

        sq_poptop(v);// pop val, so we can get the key
        childVar.pathIterator = sqIter;
        childVar.pathUiString = ToString(v, -1);
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
          tableKeyToIterator.emplace_back(KeyToTableIter{ToString(v, -1), sqIter});
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

data::ReturnCode CreateChildVariablesFromIterable(
        HSQUIRRELVM vm, PathPartConstIter begin, PathPartConstIter end,
        const data::PaginationInfo& pagination, std::vector<data::Variable>& variables)
{
  return WithVariableAtPath(vm, begin, end, [vm, &pagination, &variables]() {
    return CreateChildVariables(vm, pagination, variables);
  });
}

ReturnCode WithVariableAtPath(
        SQVM* const v, const PathPartConstIter pathBegin,
        const PathPartConstIter pathEnd, const std::function<ReturnCode()>& fn)
{
  ScopedVerifySqTop scopedVerify(v);

  if (pathBegin == pathEnd) {
    // Add the children of the variable at the top of the stack to the list.
    return fn();
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
        SDB_LOGD(kLogTag, "Array index %d out of bounds", *pathBegin);
        return ReturnCode::InvalidParameter;
      }

      sq_pushinteger(v, arrIndex);
      const auto sqRetVal = sq_get(v, -2);
      if (!SQ_SUCCEEDED(sqRetVal)) {
        SDB_LOGD(kLogTag, "Failed to get array index %d", arrIndex);
        return ReturnCode::InvalidParameter;
      }
      const auto childRetVal = WithVariableAtPath(v, pathBegin + 1, pathEnd, fn);
      sq_poptop(v);// pop value
      return childRetVal;
    }
    case OT_TABLE:
    case OT_INSTANCE:
    {
      sq_pushinteger(v, *pathBegin);
      if (!SQ_SUCCEEDED(sq_next(v, -2))) {
        SDB_LOGD(kLogTag, "Failed to read iterator %d", *pathBegin);
        sq_poptop(v);// pop iterator
        return ReturnCode::InvalidParameter;
      }
      const auto childRetVal = WithVariableAtPath(v, pathBegin + 1, pathEnd, fn);
      sq_pop(v, 3);// pop value, key and iterator
      return childRetVal;
    }
    default:
      SDB_LOGD(kLogTag, "Iterator points to non iterable type: %d", type);
      return ReturnCode::InvalidParameter;
  }
}

ReturnCode GetObjectFromExpression(
        SQVM* const v, const SqExpressionNode* expressionNode, const PaginationInfo& pagination, HSQOBJECT& foundObject,
        std::vector<uint32_t>& iteratorPath)
{
  ScopedVerifySqTop scopedVerify(v);

  if (expressionNode == nullptr) {
    // Add the variable at the top of the stack to the list.
    if (!SQ_SUCCEEDED(sq_getstackobj(v, -1, &foundObject))) {
      SDB_LOGD(kLogTag, "Failed to read object from the stack");
      return ReturnCode::ErrorInternal;
    }
    return ReturnCode::Success;
  }

  std::stringstream ss;

  // Push the indexed child on to the stack
  const auto type = sq_gettype(v, -1);
  switch (type) {
    case OT_ARRAY:
    {
      if (!sq_isnumeric(expressionNode->accessorObject)) {
        SDB_LOGD(kLogTag, "Failed to get from array, key is not numeric.");
        return ReturnCode::InvalidParameter;
      }

      sq_pushobject(v, expressionNode->accessorObject);
      SQInteger arrIndex;
      sq_getinteger(v, -1, &arrIndex);
      const auto sqRetVal = sq_get(v, -2);
      if (!SQ_SUCCEEDED(sqRetVal)) {
        SDB_LOGD(kLogTag, "Failed to get array index %d", arrIndex);
        return ReturnCode::InvalidParameter;
      }

      iteratorPath.push_back(static_cast<uint32_t>(arrIndex));
      const auto childRetVal =
              GetObjectFromExpression(v, expressionNode->next.get(), pagination, foundObject, iteratorPath);
      sq_poptop(v);// pop value
      return childRetVal;
    }
    case OT_TABLE:
    case OT_INSTANCE:
    {
      // Get the object that we're looking for, so we can iterate through all keys to find the iterator we want.
      sq_pushobject(v, expressionNode->accessorObject);
      if (!SQ_SUCCEEDED(sq_get(v, -2))) {
        SDB_LOGD(kLogTag, "Failed to read accessor");
        return ReturnCode::InvalidParameter;
      }

      SQInteger sqIter = 0;
      sq_pushinteger(v, sqIter);
      HSQOBJECT iterKey;
      while (SQ_SUCCEEDED(sq_getinteger(v, -1, &sqIter)) && SQ_SUCCEEDED(sq_next(v, -3))) {
        sq_getstackobj(v, -2, &iterKey);
        if (iterKey._unVal.raw == expressionNode->accessorObject._unVal.raw) {
          iteratorPath.push_back(static_cast<uint32_t>(sqIter));
          const auto childRetVal =
                  GetObjectFromExpression(v, expressionNode->next.get(), pagination, foundObject, iteratorPath);
          sq_pop(v, 4);// pop value, key, null iterator, initially found value
          return childRetVal;
        }
        sq_pop(v, 2);// pop value and key
      }
      sq_poptop(v);// pop null iterator
      sq_poptop(v);// pop initially found value

      // Didn't find anything
      SDB_LOGD(kLogTag, "No matching key in table");
      return ReturnCode::InvalidParameter;
    }
    default:
      SDB_LOGD(kLogTag, "Iterator points to non iterable type: %d", type);
      return ReturnCode::InvalidParameter;
  }
}

std::string ToClassFullName(SQVM* const v, const SQInteger idx)
{
  ScopedVerifySqTop scopedVerify(v);

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


std::string ReadString(std::string::const_iterator& pos, std::string::const_iterator end)
{
  const char enclosingChar = *(pos++);
  const char* eofError =
          enclosingChar == '\'' ? "Encountered EOF when looking for '" : "Encountered EOF when looking for \"";

  const auto processStringEscape = [&](std::string& dest, const int maxDigits) {
    char c = *(++pos);
    if (pos == end) {
      throw WatchParseError(eofError, pos);
    }
    if (0 == isxdigit(c)) {
      throw WatchParseError("hexadecimal number expected", pos);
    }
    int n = 0;
    while (0 != isxdigit(c) && n < maxDigits) {
      dest[n] = c;
      ++n;
      c = *(++pos);
      if (pos == end) {
        throw WatchParseError(eofError, pos);
      }
    }
  };

  std::string output;
  for (; pos != end; ++pos) {
    switch (char c = *pos; c) {
      case '\\':
        c = *(++pos);
        if (pos == end) {
          throw WatchParseError(eofError, pos);
        }
        switch (c) {
          case 't':
            output += '\t';
            break;
          case 'a':
            output += '\a';
            break;
          case 'b':
            output += '\b';
            break;
          case 'n':
            output += '\n';
            break;
          case 'r':
            output += '\r';
            break;
          case 'v':
            output += '\v';
            break;
          case 'f':
            output += '\f';
            break;
          case '0':
            output += '\0';
            break;
          case '\\':
          case '"':
          case '\'':
            output += c;
            break;
          case 'x':
          {
            const size_t maxDigits = sizeof(SQChar) * 2;
            std::string temp(maxDigits, '0');
            processStringEscape(temp, maxDigits);
            char* stemp;
            output += static_cast<SQChar>(scstrtoul(temp.c_str(), &stemp, 16));
            break;
          }
          case 'u':
          case 'U':
          {
            const size_t maxDigits = c == 'u' ? 4 : 8;
            std::string temp(maxDigits, '0');
            processStringEscape(temp, 8);
            char* stemp;
#ifdef SQUNICODE
#if WCHAR_SIZE == 2
#error not implemented
#else
#error not implemented
#endif
#else
            output += static_cast<SQChar>(scstrtoul(temp.c_str(), &stemp, 16));
#endif
            break;
          }
          default:
            throw WatchParseError("unknown escape character", pos);
        }
        break;
      case '"':
      case '\'':
        if (c == enclosingChar) {
          ++pos;
          return output;
        }
        output += c;
        break;
      case '\n':
        throw WatchParseError("newline in an inline string", pos);
      default:
        output += c;
    }
  }

  return output;
}

std::string ReadNumber(std::string::const_iterator& pos, const std::string::const_iterator end)
{
  const auto first = pos;
  for (; pos != end; ++pos) {
    const char c = *pos;
    if (0 == isdigit(c)) {
      break;
    }
  }

  return std::string(first, pos);
}

std::string ReadIdentifier(std::string::const_iterator& pos, const std::string::const_iterator end)
{
  const auto first = pos;
  for (; pos != end; ++pos) {
    const char c = *pos;
    if (0 == isalnum(c) && c != '_') {
      break;
    }
  }

  return std::string(first, pos);
}

std::unique_ptr<ExpressionNode> ParseExpression(std::string::const_iterator& pos, const std::string::const_iterator end)
{
  auto rootExpression = std::make_unique<ExpressionNode>();
  auto currentExpression = rootExpression.get();
  for (; pos != end;) {
    switch (const char c = *pos; c) {
      case ' ':
        ++pos;
        break;
      case '.':
      {
        if (currentExpression->type != ExpressionNodeType::Identifier) {
          throw WatchParseError("Attempted to access field of a non-identifier", pos);
        }

        currentExpression->next = std::make_unique<ExpressionNode>();
        currentExpression = currentExpression->next.get();
        ++pos;

        // Peek at the next character to make sure it's a valid identifier character
        if (pos == end) {
          throw WatchParseError("Expected identifier character after . but got EOF", pos);
        }

        if (0 == isalpha(*pos) && *pos != '_') {
          throw WatchParseError("Expected identifier character after .", pos);
        }

        break;
      }
      case '[':
      {
        if (currentExpression->type != ExpressionNodeType::String &&
            currentExpression->type != ExpressionNodeType::Identifier) {
          throw WatchParseError("[ must follow an identifier or string", pos);
        }

        ++pos;
        currentExpression->next = std::make_unique<ExpressionNode>();
        currentExpression = currentExpression->next.get();
        currentExpression->type = ExpressionNodeType::Identifier;

        currentExpression->accessorExpression = ParseExpression(pos, end);
        if (currentExpression->accessorExpression->type == ExpressionNodeType::Undefined) {
          throw WatchParseError("Could not create accessor expression", pos);
        }

        // The call to ReadExpression will also consume the ] so we don't need to increment it.
        break;
      }
      case ']':
      {
        if (currentExpression->type == ExpressionNodeType::Undefined) {
          throw WatchParseError("Closing square bracket without a contained expression", pos);
        }

        ++pos;
        return rootExpression;
      }
      case '"':
      case '\'':
      {
        if (currentExpression->type != ExpressionNodeType::Undefined) {
          throw WatchParseError("String must not follow another expression", pos);
        }

        currentExpression->accessorValue = ReadString(pos, end);
        currentExpression->type = ExpressionNodeType::String;

        // ReadString will increment `pos` past the end of the expression
        // so we don't need to increment it.
        break;
      }
      default:
        if (currentExpression->type != ExpressionNodeType::Undefined) {
          throw WatchParseError("Identifier or number must not directly follow another expression", pos);
        }

        if (0 != isdigit(c)) {
          // read a number
          currentExpression->type = ExpressionNodeType::Number;
          currentExpression->accessorValue = ReadNumber(pos, end);
        }
        else if (0 != isalpha(c) || c == '_') {
          // read an identifier
          currentExpression->type = ExpressionNodeType::Identifier;
          currentExpression->accessorValue = ReadIdentifier(pos, end);
        }
        else {
          throw WatchParseError("Invalid character, expected alphanumeric or underscore.", pos);
        }

        // ReadNumber and ReadIdentifier will increment `pos` past the end of the expression
        // so we don't need to increment it.
        break;
    }
  }

  return rootExpression;
}
}// namespace sdb::sq