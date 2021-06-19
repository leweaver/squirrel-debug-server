//
// Created by Lewis weaver on 5/30/2021.
//

#include "Logger.h"
#include "include/sdb/EmbeddedServer.h"
#include "dto/EventDto.h"

#include <oatpp/core/macro/component.hpp>
#include <oatpp/core/collection/LinkedList.hpp>
#include <oatpp/network/tcp/server/ConnectionProvider.hpp>
#include <oatpp/network/Server.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/HttpConnectionHandler.hpp>
#include <oatpp/web/server/HttpRouter.hpp>

#include <oatpp-swagger/Controller.hpp>

#include <iostream>
#include <sstream>

#include "controller/TestAppController.h"
#include "websocket/WSListener.h"
#include "AppComponents.h"

using oatpp::parser::json::mapping::ObjectMapper;
using oatpp::data::mapping::type::Void;

namespace sdb {

class OatMessageEventInterface : public MessageEventInterface {
 public:
  OatMessageEventInterface(std::shared_ptr<WSInstanceListener> webSocketInstanceListener)
      : webSocketInstanceListener_(webSocketInstanceListener) {}

  void OnStatus(data::Status&& status) override {
    const auto statusDto = dto::Status::createShared();
    statusDto->runstate = static_cast<sdb::dto::RunState>(status.runstate);
    statusDto->stack = oatpp::List<oatpp::Object<dto::StackEntry>>::createShared();
    for (const auto& stackEntry : status.stack) {
      const auto stackEntryDto = dto::StackEntry::createShared();
      stackEntryDto->file = stackEntry.file.c_str();
      stackEntryDto->line = stackEntry.line;
      stackEntryDto->function = stackEntry.function.c_str();
      statusDto->stack->push_back(stackEntryDto);
    }

    const auto wrapper = dto::EventMessageWrapper<dto::Status>::createShared();
    wrapper->type = dto::EventMessageType::Status;
    wrapper->message = statusDto;

    webSocketInstanceListener_->broadcastMessage(mapper_->writeToString(wrapper));
  }

 private:
  std::shared_ptr<ObjectMapper> mapper_ = ObjectMapper::createShared();
  std::shared_ptr<WSInstanceListener> webSocketInstanceListener_;
};

class EndpointImpl : public EmbeddedServer {

 public:
  void SetCommandInterface(std::shared_ptr<MessageCommandInterface> messageCommandInterface) override {

    /* create ApiControllers and add endpoints to router */
    appComponents_ = std::make_shared<AppComponents>(messageCommandInterface);

    OATPP_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>, router);
    std::shared_ptr<oatpp::web::server::api::ApiController::Endpoints> docEndpoints = oatpp::swagger::Controller::Endpoints::createShared();

    testAppController_ = TestAppController::createShared();
    testAppController_->addEndpointsToRouter(router);

    docEndpoints->pushBackAll(testAppController_->getEndpoints());

    swaggerController_ = oatpp::swagger::Controller::createShared(docEndpoints);
    swaggerController_->addEndpointsToRouter(router);

    eventInterface_ = std::make_shared<OatMessageEventInterface>(appComponents_->webSocketInstanceListener_);
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
  std::shared_ptr<AppComponents> appComponents_;

  // Controllers
  std::shared_ptr<oatpp::web::server::api::ApiController> testAppController_;
  std::shared_ptr<oatpp::swagger::Controller> swaggerController_;

  std::shared_ptr<bool> stopping_ = std::make_shared<bool>(false);
  std::thread worker_;
  static constexpr const char* TAG = "Server_Endpoint";
};

EmbeddedServer* EmbeddedServer::Create() {
  return new EndpointImpl();
}

void EmbeddedServer::InitEnvironment() {
  std::cout << OATPP_SWAGGER_RES_PATH << std::endl;
  oatpp::base::Environment::init();
  oatpp::base::Environment::setLogger(std::make_shared<DebugStrLogger>());
}

void EmbeddedServer::ShutdownEnvironment() {
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

}// namespace sdb
