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

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace sdb {
class DebugCommandController : public oatpp::web::server::api::ApiController {
 public:
  DebugCommandController(std::shared_ptr<MessageCommandInterface> messageCommandInterface,
                         std::shared_ptr<ObjectMapper> objectMapper)
      : oatpp::web::server::api::ApiController(objectMapper, "DebugCommand/"), messageCommandInterface_(messageCommandInterface), commandOkResponse_(createCommandOkResponse()) {
  }

  static std::shared_ptr<DebugCommandController> createShared(
          std::shared_ptr<MessageCommandInterface> messageCommandInterface,
          OATPP_COMPONENT(std::shared_ptr<ObjectMapper>, objectMapper)// Inject objectMapper component here as default parameter
  ) {
    return std::make_shared<DebugCommandController>(messageCommandInterface, objectMapper);
  }

  ENDPOINT("PUT", "SendStatus", sendStatus) {
    messageCommandInterface_->SendStatus();
    return commandOkResponse_;
  }
  ENDPOINT_INFO(sendStatus) {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "StepOut", stepOut) {
    messageCommandInterface_->StepOut();
    return commandOkResponse_;
  }
  ENDPOINT_INFO(stepOut) {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "StepOver", stepOver) {
    messageCommandInterface_->StepOver();
    return commandOkResponse_;
  }
  ENDPOINT_INFO(stepOver) {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "StepIn", stepIn) {
    messageCommandInterface_->StepIn();
    return commandOkResponse_;
  }
  ENDPOINT_INFO(stepIn) {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "Pause", pause) {
    messageCommandInterface_->Pause();
    return commandOkResponse_;
  }
  ENDPOINT_INFO(pause) {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

  ENDPOINT("PUT", "Continue", cont) {
    messageCommandInterface_->Continue();
    return commandOkResponse_;
  }
  ENDPOINT_INFO(cont) {
    info->addResponse<Object<dto::CommandMessageResponse>>(Status::CODE_200, "application/json");
  }

 private:
  std::shared_ptr<ApiController::OutgoingResponse> createCommandOkResponse() {
    auto dto = dto::CommandMessageResponse::createShared();
    dto->code = 0;
    return createDtoResponse(Status::CODE_200, dto);
  }

  const std::shared_ptr<MessageCommandInterface> messageCommandInterface_;
  const std::shared_ptr<ApiController::OutgoingResponse> commandOkResponse_;
};
}// namespace sdb

#include OATPP_CODEGEN_BEGIN(ApiController)
#endif//DEBUG_COMMAND_CONTROLLER_H
