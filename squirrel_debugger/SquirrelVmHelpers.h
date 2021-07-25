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

namespace sdb::sq {
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

data::ReturnCode CreateChildVariable(HSQUIRRELVM v, data::Variable& variable);
data::ReturnCode CreateChildVariablesFromIterable(
        HSQUIRRELVM v, std::vector<uint64_t>::const_iterator pathBegin, std::vector<uint64_t>::const_iterator pathEnd,
        const data::PaginationInfo& pagination, std::vector<data::Variable>& variables);
data::ReturnCode CreateChildVariableFromExpression(
        SQVM* v, const ExpressionNode* expressionNode, const data::PaginationInfo& pagination,
        data::Variable& variable, std::vector<uint32_t>& iteratorPath);

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
