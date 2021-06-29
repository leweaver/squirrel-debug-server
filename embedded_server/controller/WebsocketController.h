//
// Created by Lewis weaver on 5/30/2021.
//
#pragma once

#ifndef WEBSOCKET_CONTROLLER_H
#define WEBSOCKET_CONTROLLER_H

#include <oatpp-websocket/Handshaker.hpp>
#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/core/macro/component.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/api/ApiController.hpp>

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace sdb {
class WebsocketController final : public oatpp::web::server::api::ApiController {
 public:
  WebsocketController(const std::shared_ptr<ObjectMapper>& objectMapper) : ApiController(objectMapper) {}

  static std::shared_ptr<WebsocketController> CreateShared(OATPP_COMPONENT(std::shared_ptr<ObjectMapper>,
                                                                           objectMapper)) {
    return std::make_shared<WebsocketController>(objectMapper);
  }

  ENDPOINT("GET", "ws", WebSocket, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return oatpp::websocket::Handshaker::serversideHandshake(request->getHeaders(), websocketConnectionHandler_);
  };

 private:
  OATPP_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>, websocketConnectionHandler_, "websocket");
};
}// namespace sdb

#include OATPP_CODEGEN_BEGIN(ApiController)
#endif//WEBSOCKET_CONTROLLER_H
