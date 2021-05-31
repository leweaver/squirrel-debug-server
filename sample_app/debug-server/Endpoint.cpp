//
// Created by Lewis weaver on 5/30/2021.
//

#include "Endpoint.h"
#include "Logger.h"
#include "MessageInterface.h"
#include "SwaggerComponent.h"

#include "oatpp/core/macro/component.hpp"
#include "oatpp/network/tcp/server/ConnectionProvider.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"
#include "oatpp/web/server/HttpConnectionHandler.hpp"
#include "oatpp/web/server/HttpRouter.hpp"


#include "oatpp-swagger/Controller.hpp"
#include "oatpp/network/Server.hpp"

#include <iostream>
#include <oatpp/core/collection/LinkedList.hpp>
#include <sstream>

#include "controller/TestAppController.h"
#include "websocket/WSListener.h"


namespace qdb {
class TestAppComponent {
 public:
  explicit TestAppComponent(std::shared_ptr<MessageCommandInterface> commandInterface)
    : websocketConnectionHandler(CreateWebSocketConnectionHandler(commandInterface)) {
  }

  static oatpp::base::Environment::Component<std::shared_ptr<oatpp::network::ConnectionHandler>> 
  CreateWebSocketConnectionHandler(std::shared_ptr<MessageCommandInterface> commandInterface) {
    auto connectionHandler = oatpp::websocket::ConnectionHandler::createShared();
    connectionHandler->setSocketInstanceListener(std::make_shared<WSInstanceListener>(commandInterface));
    return oatpp::base::Environment::Component<std::shared_ptr<oatpp::network::ConnectionHandler>>(
            "websocket" /* qualifier */, connectionHandler);
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
  oatpp::base::Environment::Component<std::shared_ptr<oatpp::network::ConnectionHandler>> websocketConnectionHandler;
};

class OatMessageEventInterface : public MessageEventInterface {
 public:
  void OnBreakpointHit() override {
  }
};

class EndpointImpl : public Endpoint {

 public:
  EndpointImpl(std::shared_ptr<MessageCommandInterface> messageCommandInterface) {
    eventInterface_ = std::make_shared<OatMessageEventInterface>();

    /* create ApiControllers and add endpoints to router */
    appComponents_ = std::make_shared<TestAppComponent>(messageCommandInterface);

    auto router = appComponents_->httpRouter.getObject();
    std::shared_ptr<oatpp::web::server::api::ApiController::Endpoints> docEndpoints = oatpp::swagger::Controller::Endpoints::createShared();

    testAppController_ = TestAppController::createShared();
    testAppController_->addEndpointsToRouter(router);

    docEndpoints->pushBackAll(testAppController_->getEndpoints());

    swaggerController_ = oatpp::swagger::Controller::createShared(docEndpoints);
    swaggerController_->addEndpointsToRouter(router);
  }

  std::shared_ptr<MessageEventInterface> GetEventInterface() const override {
    return eventInterface_;
  }

  void Start() override {
    /* create server */
    worker_ = std::thread([components = this->appComponents_, stopping = this->stopping_]() {
      /* Get connection handler component */
      OATPP_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>, connectionHandler, "http");

      /* Get connection provider component */
      OATPP_COMPONENT(std::shared_ptr<oatpp::network::ServerConnectionProvider>, connectionProvider);
      oatpp::network::Server server(connectionProvider, connectionHandler);

      OATPP_LOGD(TAG, "Running on port %s...", connectionProvider->getProperty("port").toString()->c_str());

      server.run([stopping]() { return !*stopping; });

      OATPP_LOGD(TAG, "Stopped");
    });
  }

  // Stops the worker thread, optionally joining the thread until the stop has completed.
  void Stop(bool join = true) override {
    *stopping_ = true;
    if (join) {
      worker_.join();
    }
  }

  std::shared_ptr<OatMessageEventInterface> eventInterface_;
  std::shared_ptr<TestAppComponent> appComponents_;

  // Controllers
  std::shared_ptr<oatpp::web::server::api::ApiController> testAppController_;
  std::shared_ptr<oatpp::swagger::Controller> swaggerController_;

  std::shared_ptr<bool> stopping_ = std::make_shared<bool>(false);
  std::thread worker_;
  static constexpr const char* TAG = "Server_Endpoint";
};

Endpoint* Endpoint::Create(std::shared_ptr<MessageCommandInterface> messageCommandInterface) {
  return new EndpointImpl(messageCommandInterface);
}

void Endpoint::InitEnvironment() {
  std::cout << OATPP_SWAGGER_RES_PATH << std::endl;
  oatpp::base::Environment::init();
  oatpp::base::Environment::setLogger(std::make_shared<DebugStrLogger>());
}

void Endpoint::ShutdownEnvironment() {
  /* Print how much objects were created during app running, and what have left-probably leaked */
  /* Disable object counting for release builds using '-D OATPP_DISABLE_ENV_OBJECT_COUNTERS' flag for better performance */
  {
    auto logger = oatpp::base::Environment::getLogger();
    auto pri = oatpp::base::Logger::PRIORITY_D;

    std::stringstream ss;
    ss << "\nEnvironment:\n";
    ss << "objectsCount = " << oatpp::base::Environment::getObjectsCount() << "\n";
    ss << "objectsCreated = " << oatpp::base::Environment::getObjectsCreated() << "\n\n";
    OATPP_LOGD("Endpoint", ss.str().c_str());
  }

  oatpp::base::Environment::destroy();
}

}// namespace qdb
