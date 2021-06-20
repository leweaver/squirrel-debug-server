//
// Created by Lewis weaver on 5/30/2021.
//

#pragma once

#ifndef APP_COMPONENTS_H
#define APP_COMPONENTS_H


#include <oatpp/parser/json/mapping/ObjectMapper.hpp>

#include <oatpp/network/tcp/server/ConnectionProvider.hpp>
#include <oatpp/web/server/HttpConnectionHandler.hpp>

#include <oatpp/core/macro/component.hpp>

#include <sdb/MessageInterface.h>

#include "websocket/WSListener.h"
#include "SwaggerComponent.h"

namespace sdb {
/**
 *  Class which creates and holds Application components and registers components in oatpp::base::Environment
 *  Order of components initialization is from top to bottom
 */
class AppComponents {
 public:
  explicit AppComponents()
      : webSocketInstanceListener_(CreateWebSocketInstanceListener()), webSocketConnectionHandler_(CreateWebSocketConnectionHandler(webSocketInstanceListener_)) {
  }

  static oatpp::base::Environment::Component<std::shared_ptr<oatpp::network::ConnectionHandler>>
  CreateWebSocketConnectionHandler(std::shared_ptr<WSInstanceListener> instanceListener) {
    auto connectionHandler = oatpp::websocket::ConnectionHandler::createShared();
    connectionHandler->setSocketInstanceListener(instanceListener);
    return oatpp::base::Environment::Component<std::shared_ptr<oatpp::network::ConnectionHandler>>(
            "websocket" /* qualifier */, connectionHandler);
  }

  static std::shared_ptr<WSInstanceListener> CreateWebSocketInstanceListener() {
    return std::make_shared<WSInstanceListener>();
  }

  /**
   *  Swagger component
   */
  SwaggerComponent swaggerComponent;

  /**
   *  Create ConnectionProvider component which listens on the port
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::ServerConnectionProvider>, serverConnectionProvider)
  ([] {
    return oatpp::network::tcp::server::ConnectionProvider::createShared({"localhost", 8000, oatpp::network::Address::IP_4});
  }());

  /**
   *  Create Router component
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>, httpRouter)
  ([] {
    return oatpp::web::server::HttpRouter::createShared();
  }());

  /**
   *  Create http ConnectionHandler
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>, httpConnectionHandler)
  ("http" /* qualifier */, [] {
    OATPP_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>, router);// get Router component
    return oatpp::web::server::HttpConnectionHandler::createShared(router);
  }());

  /**
   * Create ObjectMapper component to serialize/deserialize DTOs in Contoller's API
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::data::mapping::ObjectMapper>, apiObjectMapper)
  ([] {
    auto objectMapper = oatpp::parser::json::mapping::ObjectMapper::createShared();
    objectMapper->getDeserializer()->getConfig()->allowUnknownFields = false;
    return objectMapper;
  }());

  /**
   *  Create websocket connection handler
   */
  std::shared_ptr<WSInstanceListener> webSocketInstanceListener_;
  oatpp::base::Environment::Component<std::shared_ptr<oatpp::network::ConnectionHandler>> webSocketConnectionHandler_;
};
}// namespace sdb::server

#endif
