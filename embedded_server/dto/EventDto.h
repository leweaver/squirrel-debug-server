//
// Created by Lewis weaver on 5/30/2021.
//
#pragma once

#ifndef SAMPLE_APP_EVENT_DTO_H
#define SAMPLE_APP_EVENT_DTO_H
#include "oatpp/codegen/dto/enum_define.hpp"
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include OATPP_CODEGEN_BEGIN(DTO)///< Begin DTO codegen section

namespace qdb::dto {
ENUM(CommandMessageType, v_int32,
     VALUE(Pause, 0, "pause"),
     VALUE(Continue, 1, "continue"),
     VALUE(StepOut, 2, "step_out"),
     VALUE(StepOver, 3, "step_over"),
     VALUE(StepIn, 4, "step_in"),
     VALUE(SendStatus, 5, "send_status"))

ENUM(EventMessageType, v_int32,
     VALUE(Status, 0, "status"))

ENUM(RunState, v_int32,
     VALUE(Running, 0, "running"),
     VALUE(Pausing, 1, "pausing"),
     VALUE(Paused, 2, "paused"),
     VALUE(Stepping, 3, "stepping"))

template<typename TMessageBody>
class EventMessageWrapper : public oatpp::DTO {
  DTO_INIT(EventMessageWrapper, DTO)

  DTO_FIELD(Enum<EventMessageType>, type);
  DTO_FIELD(Object<TMessageBody>, message);
};

template<typename TMessageBody>
class CommandMessageTypeOnly : public oatpp::DTO {
  DTO_INIT(CommandMessageTypeOnly, DTO)

  DTO_FIELD(Enum<CommandMessageType>, type);
};

template<typename TMessageBody>
class CommandMessageWrapper : public oatpp::DTO {
  DTO_INIT(CommandMessageWrapper, DTO)

  DTO_FIELD(Enum<CommandMessageType>, type);
  DTO_FIELD(Object<TMessageBody>, message);
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
}// namespace qdb::dto

#include OATPP_CODEGEN_END(DTO)///< End DTO codegen section

#endif// SAMPLE_APP_EVENT_DTO_H
