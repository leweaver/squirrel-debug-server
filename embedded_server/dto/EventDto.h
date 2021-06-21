//
// Created by Lewis weaver on 5/30/2021.
//
#pragma once

#ifndef EVENT_DTO_H
#define EVENT_DTO_H

#include <oatpp/core/Types.hpp>
#include <oatpp/core/macro/codegen.hpp>

#include OATPP_CODEGEN_BEGIN(DTO)///< Begin DTO codegen section

namespace sdb::dto {
// clang-format off
ENUM(CommandMessageType, v_int32,
     VALUE(Pause,      0, "pause"),
     VALUE(Continue,   1, "continue"),
     VALUE(StepOut,    2, "step_out"),
     VALUE(StepOver,   3, "step_over"),
     VALUE(StepIn,     4, "step_in"),
     VALUE(SendStatus, 5, "send_status"))

ENUM(EventMessageType, v_int32,
     VALUE(Status,     0, "status"))

ENUM(RunState, v_int32,
     VALUE(Running,    0, "running"),
     VALUE(Pausing,    1, "pausing"),
     VALUE(Paused,     2, "paused"),
     VALUE(Stepping,   3, "stepping"))

ENUM(VariableType, v_int32,
     VALUE(String,     0, "string"),
     VALUE(Bool,       1, "bool"),
     VALUE(Integer,    2, "integer"),
     VALUE(Float,      3, "float"),
     VALUE(Closure,    4, "closure"),
     VALUE(Class,      5, "class"),
     VALUE(Instance,   6, "instance"),
     VALUE(Array,      7, "array"),
     VALUE(Table,      8, "table"),
     VALUE(Other,      9, "other"))
// clang-format on

template<typename TMessageBody>
class EventMessageWrapper : public oatpp::DTO {
  DTO_INIT(EventMessageWrapper, DTO)

  DTO_FIELD(Enum<EventMessageType>, type);
  DTO_FIELD(Object<TMessageBody>, message);
};

class CommandMessageResponse : public oatpp::DTO {
  DTO_INIT(CommandMessageResponse, DTO)

  DTO_FIELD(Int32, code);
};

class Variable : public oatpp::DTO {
  DTO_INIT(Variable, DTO)

  DTO_FIELD(String, name);
  DTO_FIELD(Enum<VariableType>, type);
  DTO_FIELD(String, value);
};

class VariableList : public oatpp::DTO {
  DTO_INIT(VariableList, DTO)

  DTO_FIELD(List<Object<Variable>>, variables);
};

class StackEntry : public oatpp::DTO {
  DTO_INIT(StackEntry, DTO)

  DTO_FIELD(String, file);
  DTO_FIELD(Int64, line);
  DTO_FIELD(String, function);
};

class Status : public oatpp::DTO {

  DTO_INIT(Status, DTO)

  DTO_FIELD(Enum<RunState>, runstate);
  DTO_FIELD(List<Object<StackEntry>>, stack);
};
}// namespace sdb::dto

#include OATPP_CODEGEN_END(DTO)///< End DTO codegen section

#endif// EVENT_DTO_H
