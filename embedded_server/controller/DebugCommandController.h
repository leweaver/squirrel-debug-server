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
  DebugCommandController(
          std::shared_ptr<MessageCommandInterface> messageCommandInterface,
          const std::shared_ptr<ObjectMapper>& objectMapper)
      : ApiController(objectMapper, "DebugCommand/")
      , messageCommandInterface_(std::move(messageCommandInterface))
      , commandOkResponse_(CreateCommandOkResponse())
  {}

  static std::shared_ptr<DebugCommandController> CreateShared(
          std::shared_ptr<MessageCommandInterface> messageCommandInterface,
          OATPP_COMPONENT(std::shared_ptr<ObjectMapper>, objectMapper))
  {
    return std::make_shared<DebugCommandController>(std::move(messageCommandInterface), objectMapper);
  }

  ENDPOINT("PUT", "SendStatus", SendStatus)
  {
    return CreateReturnCodeResponse(messageCommandInterface_->SendStatus());
  }
  ENDPOINT_INFO(SendStatus)
  {
    AddCommandMessageResponse(info);
  }

  ENDPOINT("PUT", "StepOut", StepOut)
  {
    return CreateReturnCodeResponse(messageCommandInterface_->StepOut());
  }
  ENDPOINT_INFO(StepOut)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "StepOver", StepOver)
  {
    return CreateReturnCodeResponse(messageCommandInterface_->StepOver());
  }
  ENDPOINT_INFO(StepOver)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "StepIn", StepIn)
  {
    return CreateReturnCodeResponse(messageCommandInterface_->StepIn());
  }
  ENDPOINT_INFO(StepIn)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "Pause", Pause)
  {
    return CreateReturnCodeResponse(messageCommandInterface_->PauseExecution());
  }
  ENDPOINT_INFO(Pause)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "Continue", Continue)
  {
    return CreateReturnCodeResponse(messageCommandInterface_->ContinueExecution());
  }
  ENDPOINT_INFO(Continue)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT(
          "GET", "Variables/Local/{stackFrame}", StackLocals, PATH(Int32, stackFrame), QUERY(String, path),
          QUERIES(QueryParams, queryParams))
  {
    return HandleVariablesCommandMessage(queryParams, [&](const data::PaginationInfo& pagination) {
      std::vector<data::Variable> variables;
      return std::tuple(
              messageCommandInterface_->GetStackVariables(stackFrame, path->std_str(), pagination, variables),
              variables);
    });
  }
  ENDPOINT_INFO(StackLocals)
  {
    info->addResponse<Object<dto::VariableList>>(Status::CODE_200, "application/json");
    AddCommandMessagePaginationParams(info);
    AddCommandMessageErrorResponses(info);
  }

  ENDPOINT("GET", "Variables/Global", StackGlobals, QUERY(String, path), QUERIES(QueryParams, queryParams))
  {
    return HandleVariablesCommandMessage(queryParams, [&](const data::PaginationInfo& pagination) {
      std::vector<data::Variable> variables;
      return std::tuple(
              messageCommandInterface_->GetGlobalVariables(path->std_str(), pagination, variables), variables);
    });
  }
  ENDPOINT_INFO(StackGlobals)
  {
    info->addResponse<Object<dto::VariableList>>(Status::CODE_200, "application/json");
    AddCommandMessagePaginationParams(info);
    AddCommandMessageErrorResponses(info);
  }

  ENDPOINT(
          "PUT", "FileBreakpoints/{file}", FileBreakpoints, PATH(String, file),
          BODY_DTO(List<Object<dto::CreateBreakpoint>>, createBpList))
  {
    std::vector<data::CreateBreakpoint> bpList;
    for (const auto& bpDto : *createBpList) {
      bpList.emplace_back(data::CreateBreakpoint{bpDto->id, bpDto->line});
    }

    std::vector<data::ResolvedBreakpoint> resolvedBpList;
    const data::ReturnCode rc =
            messageCommandInterface_->SetFileBreakpoints(std::string(file->c_str()), bpList, resolvedBpList);
    if (rc != data::ReturnCode::Success) {
      return CreateReturnCodeResponse(rc);
    }

    const auto resolvedBpListDto = dto::ResolvedBreakpointListResponse::createShared();
    resolvedBpListDto->code = static_cast<int32_t>(data::ReturnCode::Success);
    resolvedBpListDto->breakpoints = List<Object<dto::ResolvedBreakpoint>>::createShared();
    for (const auto [id, line, resolved] : resolvedBpList) {
      auto resolvedBpDto = Object<dto::ResolvedBreakpoint>::createShared();
      resolvedBpDto->id = id;
      resolvedBpDto->line = line;
      resolvedBpDto->resolved = resolved;
      resolvedBpListDto->breakpoints->emplace_back(std::move(resolvedBpDto));
    }

    return createDtoResponse(Status::CODE_200, resolvedBpListDto);
  }
  ENDPOINT_INFO(FileBreakpoints)
  {
    info->addResponse<Object<dto::ResolvedBreakpointListResponse>>(Status::CODE_200, "application/json");
    AddCommandMessageErrorResponses(info);
  }

 private:
  static void AddCommandMessageResponse(const std::shared_ptr<Endpoint::Info>& info)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
    AddCommandMessageErrorResponses(info);
  }
  static void AddCommandMessageErrorResponses(const std::shared_ptr<Endpoint::Info>& info)
  {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_400, "application/json");
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_500, "application/json");
  }
  static void AddCommandMessagePaginationParams(const std::shared_ptr<Endpoint::Info>& info)
  {
    auto& beginIteratorParam = info->queryParams.add<UInt32>("beginIterator");
    beginIteratorParam.required = false;
    beginIteratorParam.description = "Start at given pathIterator for pagination. Provide 0 to start at the beginning.";

    auto& countParam = info->queryParams.add<UInt32>("count");
    countParam.required = false;
    countParam.description = "Count of items for pagination. Count must be at most 1000.";
  }

  [[nodiscard]] static bool ParseQueryParamWithDefault(
          const QueryParams& queryParams, const char* name, const uint32_t defaultValue, uint32_t& parsedValue)
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

  [[nodiscard]] std::shared_ptr<OutgoingResponse>
  HandleVariablesCommandMessage(const QueryParams& queryParams, const VariablesCallback& getVariablesFn) const
  {
    data::PaginationInfo pagination = {};
    const bool validParams = ParseQueryParamWithDefault(queryParams, "beginIterator", 0U, pagination.beginIterator) &&
                             ParseQueryParamWithDefault(queryParams, "count", 100U, pagination.count) &&
                             pagination.count <= 1000U;
    if (!validParams) {
      return CreateReturnCodeResponse(data::ReturnCode::InvalidParameter);
    }

    const auto [ret, variables] = getVariablesFn(pagination);
    if (ret != data::ReturnCode::Success) {
      return CreateReturnCodeResponse(ret);
    }

    const auto varListDto = dto::VariableList::createShared();
    varListDto->variables = CreateVariablesList(variables);
    varListDto->code = static_cast<int32_t>(data::ReturnCode::Success);
    return createDtoResponse(Status::CODE_200, varListDto);
  }

  [[nodiscard]] static List<Object<dto::Variable>> CreateVariablesList(const std::vector<data::Variable>& variables)
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
      variableDto->instanceClassName = String(
              variable.instanceClassName.c_str(), static_cast<v_buff_size>(variable.instanceClassName.size()), false);

      variablesDto->emplace_back(std::move(variableDto));
    }
    return variablesDto;
  }

  [[nodiscard]] std::shared_ptr<OutgoingResponse> CreateCommandOkResponse() const
  {
    auto responseDto = dto::CommandMessageResponse::createShared();
    responseDto->code = 0;
    return createDtoResponse(Status::CODE_200, responseDto);
  }

  [[nodiscard]] std::shared_ptr<OutgoingResponse> CreateReturnCodeResponse(const data::ReturnCode returnCode) const
  {
    if (returnCode == data::ReturnCode::Success) {
      return commandOkResponse_;
    }

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
