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
  using VariablesCallback = std::function<std::tuple<data::ReturnCode, std::vector<data::Variable>>(
          const data::PaginationInfo& pagination)>;

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

  ENDPOINT("PUT", "SendStatus", sendStatus) { return createReturnCodeResponse(messageCommandInterface_->sendStatus()); }
  ENDPOINT_INFO(sendStatus) { addCommandMessageResponse(info); }

  ENDPOINT("PUT", "StepOut", stepOut) { return createReturnCodeResponse(messageCommandInterface_->stepOut()); }
  ENDPOINT_INFO(stepOut)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "StepOver", stepOver) { return createReturnCodeResponse(messageCommandInterface_->stepOver()); }
  ENDPOINT_INFO(stepOver)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "StepIn", stepIn) { return createReturnCodeResponse(messageCommandInterface_->stepIn()); }
  ENDPOINT_INFO(stepIn)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "Pause", pause) { return createReturnCodeResponse(messageCommandInterface_->pauseExecution()); }
  ENDPOINT_INFO(pause) { info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json"); }

  ENDPOINT("PUT", "Continue", cont) { return createReturnCodeResponse(messageCommandInterface_->continueExecution()); }
  ENDPOINT_INFO(cont) { info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json"); }

  ENDPOINT("GET", "Variables/Local/{stackFrame}", stackLocals, PATH(Int32, stackFrame), QUERY(String, path),
           QUERIES(QueryParams, queryParams))
  {
    return handleVariablesCommandMessage(queryParams, [&](const data::PaginationInfo& pagination) {
      std::vector<data::Variable> variables;
      return std::tuple(messageCommandInterface_->getStackVariables(stackFrame, path->std_str(), pagination, variables),
                        variables);
    });
  }
  ENDPOINT_INFO(stackLocals)
  {
    info->addResponse<Object<dto::VariableList>>(Status::CODE_200, "application/json");
    addCommandMessagePaginationParams(info);
    addCommandMessageErrorResponses(info);
  }

  ENDPOINT("GET", "Variables/Global", stackGlobals, QUERY(String, path), QUERIES(QueryParams, queryParams))
  {
    return handleVariablesCommandMessage(queryParams, [&](const data::PaginationInfo& pagination) {
      std::vector<data::Variable> variables;
      return std::tuple(messageCommandInterface_->getGlobalVariables(path->std_str(), pagination, variables),
                        variables);
    });
  }
  ENDPOINT_INFO(stackGlobals)
  {
    info->addResponse<Object<dto::VariableList>>(Status::CODE_200, "application/json");
    addCommandMessagePaginationParams(info);
    addCommandMessageErrorResponses(info);
  }

  ENDPOINT("PUT", "FileBreakpoints/{file}", fileBreakpoints, PATH(String, file),
           BODY_DTO(List<Object<dto::CreateBreakpoint>>, createBpList))
  {
    std::vector<data::CreateBreakpoint> bpList;
    for (const auto& bpDto : *createBpList) { bpList.emplace_back(data::CreateBreakpoint{bpDto->id, bpDto->line}); }

    std::vector<data::ResolvedBreakpoint> resolvedBpList;
    const data::ReturnCode rc =
            messageCommandInterface_->setFileBreakpoints(std::string(file->c_str()), bpList, resolvedBpList);
    if (rc != data::ReturnCode::Success) { return createReturnCodeResponse(rc); }

    const auto resolvedBpListDto = dto::ResolvedBreakpointListResponse::createShared();
    resolvedBpListDto->code = static_cast<int32_t>(data::ReturnCode::Success);
    resolvedBpListDto->breakpoints = List<Object<dto::ResolvedBreakpoint>>::createShared();
    for (const auto& resolvedBp : resolvedBpList) {
      auto resolvedBpDto = Object<dto::ResolvedBreakpoint>::createShared();
      resolvedBpDto->id = resolvedBp.id;
      resolvedBpDto->line = resolvedBp.line;
      resolvedBpDto->resolved = resolvedBp.resolved;
      resolvedBpListDto->breakpoints->emplace_back(std::move(resolvedBpDto));
    }

    return createDtoResponse(Status::CODE_200, resolvedBpListDto);
  }
  ENDPOINT_INFO(fileBreakpoints)
  {
    info->addResponse<Object<dto::ResolvedBreakpointListResponse>>(Status::CODE_200, "application/json");
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
  static void addCommandMessagePaginationParams(const std::shared_ptr<Endpoint::Info>& info)
  {
    auto& beginIteratorParam = info->queryParams.add<UInt32>("beginIterator");
    beginIteratorParam.required = false;
    beginIteratorParam.description = "Start at given pathIterator for pagination. Provide 0 to start at the beginning.";

    auto& countParam = info->queryParams.add<UInt32>("count");
    countParam.required = false;
    countParam.description = "Count of items for pagination. Count must be at most 1000.";
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

  std::shared_ptr<OutgoingResponse> handleVariablesCommandMessage(const QueryParams& queryParams,
                                                                  const VariablesCallback& getVariablesFn) const
  {
    data::PaginationInfo pagination = {};
    const bool validParams = parseQueryParamWithDefault(queryParams, "beginIterator", 0U, pagination.beginIterator) &&
                             parseQueryParamWithDefault(queryParams, "count", 100U, pagination.count) &&
                             pagination.count <= 1000U;
    if (!validParams) { return createReturnCodeResponse(data::ReturnCode::InvalidParameter); }

    const auto [ret, variables] = getVariablesFn(pagination);
    if (ret != data::ReturnCode::Success) { return createReturnCodeResponse(ret); }

    const auto varListDto = dto::VariableList::createShared();
    varListDto->variables = createVariablesList(variables);
    varListDto->code = static_cast<int32_t>(data::ReturnCode::Success);
    return createDtoResponse(Status::CODE_200, varListDto);
  }

  static List<Object<dto::Variable>> createVariablesList(const std::vector<data::Variable>& variables)
  {
    auto variablesDto = List<Object<dto::Variable>>::createShared();
    for (const auto& variable : variables) {
      auto variableDto = dto::Variable::createShared();
      variableDto->pathIterator = variable.pathIterator;
      variableDto->pathUiString =
              String(variable.pathUiString.c_str(), static_cast<v_buff_size>(variable.pathUiString.size()), false);
      variableDto->pathTableKeyType = static_cast<dto::VariableType>(variable.pathTableKeyType);
      variableDto->valueType = static_cast<dto::VariableType>(variable.valueType);
      variableDto->value = String(variable.value.c_str(), static_cast<v_buff_size>(variable.value.size()), false);
      variableDto->valueRawAddress = variable.valueRawAddress;
      variableDto->childCount = variable.childCount;
      variableDto->instanceClassName = String(variable.instanceClassName.c_str(),
                                              static_cast<v_buff_size>(variable.instanceClassName.size()), false);

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
