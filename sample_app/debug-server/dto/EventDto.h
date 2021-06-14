//
// Created by Lewis weaver on 5/30/2021.
//
#pragma once

#ifndef SAMPLE_APP_EVENT_DTO_H
#define SAMPLE_APP_EVENT_DTO_H
#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/codegen/dto/enum_define.hpp"

#include OATPP_CODEGEN_BEGIN(DTO) ///< Begin DTO codegen section

namespace qdb::dto {
ENUM(EventMessageType, v_int32,
     VALUE(Status, 0, "status"))

template <typename TMessageBody>
class MessageWrapper : public oatpp::DTO {
  DTO_INIT(MessageWrapper, DTO)

  DTO_FIELD(Enum<EventMessageType>, type);
  DTO_FIELD(Object<TMessageBody>, message);
};

ENUM(RunState, v_int32,
    VALUE(Running, 0, "running"),
    VALUE(Pausing, 1, "pausing"),
    VALUE(Paused, 2, "paused")
)

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
}// namespace qdb

#include OATPP_CODEGEN_END(DTO) ///< End DTO codegen section

#endif// SAMPLE_APP_EVENT_DTO_H
