//
// Created by Lewis weaver on 5/31/2021.
//
#pragma once

#ifndef SDB_SQUIRREL_VM_HELPERS_H
#define SDB_SQUIRREL_VM_HELPERS_H

#include "sdb/MessageInterface.h"

#include <cassert>
#include <squirrel.h>
#include <stdexcept>

#include <string>
#include <functional>

namespace sdb::sq {

using PathPartConstIter = std::vector<uint64_t>::const_iterator;

enum class ExpressionNodeType { Undefined, String, Number, Identifier };

struct ExpressionNode {
  std::unique_ptr<ExpressionNode> next;
  std::string accessorValue;
  std::unique_ptr<ExpressionNode> accessorExpression;
  ExpressionNodeType type = ExpressionNodeType::Undefined;
};

const char* ToSqObjectTypeName(SQObjectType sqType);
data::VariableType ToVariableType(SQObjectType sqType);

std::string ToClassFullName(HSQUIRRELVM v, SQInteger idx);
std::string ToString(HSQUIRRELVM v, SQInteger idx);

data::ReturnCode UpdateFromString(SQVM* const v, SQInteger objIdx, const std::string& value);

data::ReturnCode CreateChildVariable(HSQUIRRELVM v, data::Variable& variable);
data::ReturnCode CreateChildVariablesFromIterable(
        HSQUIRRELVM v, PathPartConstIter pathBegin, PathPartConstIter pathEnd,
        const data::PaginationInfo& pagination, std::vector<data::Variable>& variables);
data::ReturnCode WithVariableAtPath(SQVM* v, PathPartConstIter pathBegin, PathPartConstIter pathEnd, const std::function<data::ReturnCode()>& fn);

struct SqExpressionNode {
  std::unique_ptr<SqExpressionNode> next;
  HSQOBJECT accessorObject = {};
};
data::ReturnCode GetObjectFromExpression(
        SQVM* v, const SqExpressionNode* expressionNode, const data::PaginationInfo& pagination, HSQOBJECT& foundObject,
        std::vector<uint32_t>& iteratorPath);

class ScopedVerifySqTop {
 public:
  explicit ScopedVerifySqTop(SQVM* const vm)
      : vm_(vm)
  {
    initialDepth_ = sq_gettop(vm);
  }
  ~ScopedVerifySqTop()
  {
    const auto currentDepth = sq_gettop(vm_);
    assert(currentDepth == initialDepth_);
  }

  // Deleted methods
  ScopedVerifySqTop(const ScopedVerifySqTop& other) = delete;
  ScopedVerifySqTop(const ScopedVerifySqTop&& other) = delete;
  ScopedVerifySqTop& operator=(const ScopedVerifySqTop&) = delete;
  ScopedVerifySqTop& operator=(ScopedVerifySqTop&&) = delete;

 private:
  HSQUIRRELVM vm_;
  SQInteger initialDepth_;
};


class WatchParseError : public std::runtime_error {
 public:
  WatchParseError(const char* msg, const std::string::const_iterator pos)
      : std::runtime_error(msg)
      , pos(pos)
  {}
  const std::string::const_iterator pos;
};
std::unique_ptr<ExpressionNode> ParseExpression(std::string::const_iterator& pos, std::string::const_iterator end);

}// namespace sdb::sq

#endif// SDB_SQUIRREL_VM_HELPERS_H
