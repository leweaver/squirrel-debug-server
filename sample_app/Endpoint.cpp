//
// Created by Lewis weaver on 5/30/2021.
//

#include "Endpoint.h"
#include "SwaggerComponent.h"

#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/server/HttpRouter.hpp"
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"
#include "oatpp/core/macro/component.hpp"

#include "controller/TestAppController.h"
#include "oatpp-swagger/Controller.hpp"
#include "oatpp/network/Server.hpp"

#include <iostream>
#include <oatpp/core/collection/LinkedList.hpp>

class TestAppComponent {
 public:
  /**
   *  Swagger component
   */
  SwaggerComponent swaggerComponent;

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
   *  Create ConnectionProvider component which listens on the port
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::ServerConnectionProvider>, serverConnectionProvider)([] {
    return oatpp::network::tcp::server::ConnectionProvider::createShared({"localhost", 8000, oatpp::network::Address::IP_4});
  }());

  /**
   *  Create Router component
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>, httpRouter)([] {
    return oatpp::web::server::HttpRouter::createShared();
  }());

  /**
   *  Create ConnectionHandler component which uses Router component to route requests
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>, serverConnectionHandler)([] {
    OATPP_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>, router); // get Router component
    return oatpp::web::server::HttpConnectionHandler::createShared(router);
  }());
};

void Endpoint::InitEnvironment() {
  std::cout << OATPP_SWAGGER_RES_PATH << std::endl;
  oatpp::base::Environment::init();
}

void Endpoint::ShutdownEnvironment() {
  /* Print how much objects were created during app running, and what have left-probably leaked */
  /* Disable object counting for release builds using '-D OATPP_DISABLE_ENV_OBJECT_COUNTERS' flag for better performance */
  std::cout << "\nEnvironment:\n";
  std::cout << "objectsCount = " << oatpp::base::Environment::getObjectsCount() << "\n";
  std::cout << "objectsCreated = " << oatpp::base::Environment::getObjectsCreated() << "\n\n";

  oatpp::base::Environment::destroy();
}

void Endpoint::Start() {
  /* create ApiControllers and add endpoints to router */

  TestAppComponent components;

  auto router = components.httpRouter.getObject();
  auto docEndpoints = oatpp::swagger::Controller::Endpoints::createShared();

  auto testAppController = TestAppController::createShared();
  testAppController->addEndpointsToRouter(router);

  docEndpoints->pushBackAll(testAppController->getEndpoints());

  auto swaggerController = oatpp::swagger::Controller::createShared(docEndpoints);
  swaggerController->addEndpointsToRouter(router);

  /* Get connection handler component */
  OATPP_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>, connectionHandler);

  /* Get connection provider component */
  OATPP_COMPONENT(std::shared_ptr<oatpp::network::ServerConnectionProvider>, connectionProvider);

  /* create server */
  oatpp::network::Server server(connectionProvider,
                                connectionHandler);

  OATPP_LOGD("Server", "Running on port %s...", connectionProvider->getProperty("port").toString()->c_str());

  server.run();
}
