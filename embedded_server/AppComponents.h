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

#include "RequestErrorHandler.h"
#include "SwaggerComponent.h"
#include "websocket/WSListener.h"

namespace sdb {

const char* const kDefaultHostname = "localhost";
const uint32_t kDefaultPort = 8000U;

/**
 *  Class which creates and holds Application components and registers components in oatpp::base::Environment
 *  Order of components initialization is from top to bottom
 */
struct AppComponents {
  explicit AppComponents()
      : webSocketInstanceListener(CreateWebSocketInstanceListener())
      , webSocketConnectionHandler(CreateWebSocketConnectionHandler(webSocketInstanceListener))
  {}

  static oatpp::base::Environment::Component<std::shared_ptr<oatpp::network::ConnectionHandler>>
  CreateWebSocketConnectionHandler(const std::shared_ptr<WSInstanceListener>& instanceListener)
  {
    auto connectionHandler = oatpp::websocket::ConnectionHandler::createShared();
    connectionHandler->setSocketInstanceListener(instanceListener);
    return oatpp::base::Environment::Component<std::shared_ptr<oatpp::network::ConnectionHandler>>(
            "websocket" /* qualifier */, connectionHandler);
  }

  static std::shared_ptr<WSInstanceListener> CreateWebSocketInstanceListener()
  {
    return std::make_shared<WSInstanceListener>();
  }

  /*
   *  Swagger component
   */
  SwaggerComponent swaggerComponent;

  /*
   *  Create ConnectionProvider component which listens on the port
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::ServerConnectionProvider>, serverConnectionProvider)
  ([] {
    return oatpp::network::tcp::server::ConnectionProvider::createShared(
            {kDefaultHostname, kDefaultPort, oatpp::network::Address::IP_4});
  }());

  /*
   *  Create ErrorHandler
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::web::server::handler::ErrorHandler>, errorHandler)
  ([] {
    return std::static_pointer_cast<oatpp::web::server::handler::ErrorHandler>(RequestErrorHandler::CreateShared());
  }());

  /*
   *  Create Router component
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>, httpRouter)
  ([] { return oatpp::web::server::HttpRouter::createShared(); }());

  /*
   *  Create http ConnectionHandler
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>, httpConnectionHandler)
  ("http" /* qualifier */, [] {
    OATPP_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>, router);// get Router component
    return oatpp::web::server::HttpConnectionHandler::createShared(router);
  }());

  /*
   * Create ObjectMapper component to serialize/deserialize DTOs in Controller's API
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::data::mapping::ObjectMapper>, apiObjectMapper)
  ([] {
    auto objectMapper = oatpp::parser::json::mapping::ObjectMapper::createShared();
    objectMapper->getDeserializer()->getConfig()->allowUnknownFields = false;
    return objectMapper;
  }());

  std::shared_ptr<WSInstanceListener> webSocketInstanceListener;
  oatpp::base::Environment::Component<std::shared_ptr<oatpp::network::ConnectionHandler>> webSocketConnectionHandler;
};
}// namespace sdb

#endif
