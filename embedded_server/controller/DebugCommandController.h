//
// Created by Lewis weaver on 5/30/2021.
//
#pragma once

#ifndef DEBUG_COMMAND_CONTROLLER_H
#define DEBUG_COMMAND_CONTROLLER_H

#include <sdb/MessageInterface.h>

#include <oatpp-websocket/Handshaker.hpp>
#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/core/macro/component.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/api/ApiController.hpp>
#include <utility>

#include "../dto/EventDto.h"

#include <sstream>

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace sdb {
class DebugCommandController final : public oatpp::web::server::api::ApiController {
 public:
  DebugCommandController(std::shared_ptr<MessageCommandInterface> messageCommandInterface,
                         const std::shared_ptr<ObjectMapper>& objectMapper)
      : ApiController(objectMapper, "DebugCommand/")
      , messageCommandInterface_(std::move(messageCommandInterface))
      , commandOkResponse_(createCommandOkResponse())
  {}

  static std::shared_ptr<DebugCommandController>
  createShared(std::shared_ptr<MessageCommandInterface> messageCommandInterface,
               OATPP_COMPONENT(std::shared_ptr<ObjectMapper>, objectMapper))
  {
    return std::make_shared<DebugCommandController>(std::move(messageCommandInterface), objectMapper);
  }

  ENDPOINT("PUT", "SendStatus", sendStatus) { return createReturnCodeResponse(messageCommandInterface_->SendStatus()); }
  ENDPOINT_INFO(sendStatus) { addCommandMessageResponse(info); }

  ENDPOINT("PUT", "StepOut", stepOut) { return createReturnCodeResponse(messageCommandInterface_->StepOut()); }
  ENDPOINT_INFO(stepOut)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "StepOver", stepOver) { return createReturnCodeResponse(messageCommandInterface_->StepOver()); }
  ENDPOINT_INFO(stepOver)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "StepIn", stepIn) { return createReturnCodeResponse(messageCommandInterface_->StepIn()); }
  ENDPOINT_INFO(stepIn)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "Pause", pause) { return createReturnCodeResponse(messageCommandInterface_->Pause()); }
  ENDPOINT_INFO(pause) { info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json"); }

  ENDPOINT("PUT", "Continue", cont) { return createReturnCodeResponse(messageCommandInterface_->Continue()); }
  ENDPOINT_INFO(cont) { info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json"); }

  ENDPOINT("GET", "StackLocals/{stackFrame}", stackLocals, PATH(Int32, stackFrame), QUERY(String, path),
           QUERIES(QueryParams, queryParams))
  {
    std::vector<data::Variable> variables;

    data::PaginationInfo pagination = {};
    const bool validParams = parseQueryParamWithDefault(queryParams, "beginIndex", 0U, pagination.beginIndex) &&
                             parseQueryParamWithDefault(queryParams, "count", 100U, pagination.count) &&
                             pagination.count <= 1000U;
    if (!validParams) { return createReturnCodeResponse(data::ReturnCode::InvalidParameter); }

    const auto ret = messageCommandInterface_->GetStackLocals(stackFrame, path->std_str(), pagination, variables);
    if (ret != data::ReturnCode::Success) { return createReturnCodeResponse(ret); }

    const auto varListDto = dto::VariableList::createShared();
    varListDto->variables = createVariablesList(variables);
    return createDtoResponse(Status::CODE_200, varListDto);
  }
  ENDPOINT_INFO(stackLocals)
  {
    info->addResponse<Object<dto::VariableList>>(Status::CODE_200, "application/json");

    auto& beginIndexParam = info->queryParams.add<UInt32>("beginIndex");
    beginIndexParam.required = false;
    beginIndexParam.description = "Start index for pagination (zero based).";

    auto& countParam = info->queryParams.add<UInt32>("count");
    countParam.required = false;
    countParam.description = "Count of items for pagination. Count must be at most 1000.";

    addCommandMessageErrorResponses(info);
  }

 private:
  static void addCommandMessageResponse(const std::shared_ptr<Endpoint::Info>& info)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
    addCommandMessageErrorResponses(info);
  }
  static void addCommandMessageErrorResponses(const std::shared_ptr<Endpoint::Info>& info)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_400, "application/json");
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_500, "application/json");
  }

  [[nodiscard]] static bool parseQueryParamWithDefault(const QueryParams& queryParams, const char* name,
                                                       uint32_t defaultValue, uint32_t& parsedValue)
  {
    const auto paramValueStr = queryParams.get(name);
    if (paramValueStr == nullptr || paramValueStr->getSize() == 0) {
      parsedValue = defaultValue;
      return true;
    }

    std::stringstream ss(paramValueStr->c_str());
    ss >> parsedValue;
    return !ss.fail();
  }

  List<Object<dto::Variable>> createVariablesList(const std::vector<data::Variable>& variables) const
  {
    auto variablesDto = List<Object<dto::Variable>>::createShared();
    for (const auto& [name, type, value, children, childCount] : variables) {
      auto variableDto = dto::Variable::createShared();
      variableDto->name = String(name.c_str(), static_cast<v_buff_size>(name.size()), false);
      variableDto->type = static_cast<dto::VariableType>(type);
      variableDto->value = String(value.c_str(), static_cast<v_buff_size>(value.size()), false);
      if (!children.empty()) { variableDto->children = createVariablesList(children); }
      variableDto->childCount = childCount;
      variablesDto->emplace_back(std::move(variableDto));
    }
    return variablesDto;
  }

  std::shared_ptr<OutgoingResponse> createCommandOkResponse() const
  {
    auto responseDto = dto::CommandMessageResponse::createShared();
    responseDto->code = 0;
    return createDtoResponse(Status::CODE_200, responseDto);
  }

  std::shared_ptr<OutgoingResponse> createReturnCodeResponse(const data::ReturnCode returnCode) const
  {
    if (returnCode == data::ReturnCode::Success) { return commandOkResponse_; }

    auto responseDto = dto::CommandMessageResponse::createShared();
    responseDto->code = static_cast<int32_t>(returnCode);
    const auto status = responseDto->code >= static_cast<int32_t>(data::ReturnCode::ErrorInternal) ? Status::CODE_500
                                                                                                   : Status::CODE_400;
    return createDtoResponse(status, responseDto);
  }

  const std::shared_ptr<MessageCommandInterface> messageCommandInterface_;
  const std::shared_ptr<OutgoingResponse> commandOkResponse_;
};
}// namespace sdb

#include OATPP_CODEGEN_BEGIN(ApiController)
#endif//DEBUG_COMMAND_CONTROLLER_H
