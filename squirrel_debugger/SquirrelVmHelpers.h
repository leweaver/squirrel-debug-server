//
// Created by Lewis weaver on 5/31/2021.
//
#pragma once

#ifndef SDB_SQUIRREL_VM_HELPERS_H
#define SDB_SQUIRREL_VM_HELPERS_H

#include "sdb/MessageInterface.h"

#include <cassert>
#include <squirrel.h>

#include <string>

namespace sdb::sq {
const char* toSqObjectTypeName(SQObjectType sqType);
data::VariableType toVariableType(SQObjectType sqType);

std::string classFullName(HSQUIRRELVM v, SQInteger idx);
std::string toString(HSQUIRRELVM v, SQInteger idx);

data::ReturnCode createChildVariable(HSQUIRRELVM v, data::Variable& variable);
data::ReturnCode createChildVariablesFromIterable(
        HSQUIRRELVM v, std::vector<uint64_t>::const_iterator pathBegin, std::vector<uint64_t>::const_iterator pathEnd,
        const data::PaginationInfo& pagination, std::vector<data::Variable>& variables);

class ScopedVerifySqTop {
 public:
  explicit ScopedVerifySqTop(SQVM* const vm)
      : vm_(vm)
  {
    initialDepth_ = sq_gettop(vm);
  }
  ~ScopedVerifySqTop()
  {
    assert(sq_gettop(vm_) == initialDepth_);
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

}// namespace sdb::sq

#endif// SDB_SQUIRREL_VM_HELPERS_H
