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
class WebsocketController : public oatpp::web::server::api::ApiController {
 public:
  WebsocketController(const std::shared_ptr<ObjectMapper>& objectMapper)
      : oatpp::web::server::api::ApiController(objectMapper) {}

  static std::shared_ptr<WebsocketController> createShared(
          OATPP_COMPONENT(std::shared_ptr<ObjectMapper>, objectMapper)// Inject objectMapper component here as default parameter
  ) {
    return std::make_shared<WebsocketController>(objectMapper);
  }

  ENDPOINT("GET", "ws", ws, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return oatpp::websocket::Handshaker::serversideHandshake(request->getHeaders(), websocketConnectionHandler);
  };

 private:
  OATPP_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>, websocketConnectionHandler, "websocket");
};
}// namespace sdb

#include OATPP_CODEGEN_BEGIN(ApiController)
#endif//WEBSOCKET_CONTROLLER_H
