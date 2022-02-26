//
// Created by Lewis weaver on 5/30/2021.
//

#include "ForwardingLogger.h"
#include "include/sdb/EmbeddedServer.h"
#include "dto/EventDto.h"

#include <oatpp/core/macro/component.hpp>
#include <oatpp/core/collection/LinkedList.hpp>
#include <oatpp/network/tcp/server/ConnectionProvider.hpp>
#include <oatpp/network/Server.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/HttpConnectionHandler.hpp>
#include <oatpp/web/server/HttpRouter.hpp>

#ifdef SDB_ENABLE_OATPP_SWAGGER
#include <oatpp-swagger/Controller.hpp>
#endif

#include <iostream>

#include "controller/DebugCommandController.h"
#include "controller/StaticController.h"
#include "controller/WebsocketController.h"
#include "websocket/WSListener.h"

#include "AppComponents.h"

using oatpp::parser::json::mapping::ObjectMapper;
using oatpp::data::mapping::type::Void;
using oatpp::data::mapping::type::String;

namespace sdb {

class OatMessageEventInterface : public MessageEventInterface {
 public:
  explicit OatMessageEventInterface(std::shared_ptr<WSInstanceListener> webSocketInstanceListener)
      : webSocketInstanceListener_(std::move(webSocketInstanceListener)) {}

  void HandleStatusChanged(const data::Status& status) override {
    const auto statusDto = dto::Status::createShared();
    statusDto->runstate = static_cast<sdb::dto::RunState>(status.runState);
    statusDto->stack = oatpp::List<oatpp::Object<dto::StackEntry>>::createShared();
    for (const auto& stackEntry : status.stack) {
      const auto stackEntryDto = dto::StackEntry::createShared();
      stackEntryDto->file = stackEntry.file.c_str();
      stackEntryDto->line = stackEntry.line;
      stackEntryDto->function = stackEntry.function.c_str();
      statusDto->stack->push_back(stackEntryDto);
    }
    statusDto->pausedAtBreakpointId = status.pausedAtBreakpointId;

    const auto wrapper = dto::EventMessageWrapper<dto::Status>::createShared();
    wrapper->type = dto::EventMessageType::Status;
    wrapper->message = statusDto;

    webSocketInstanceListener_->broadcastMessage(mapper_->writeToString(wrapper));
  }

  void HandleOutputLine(const data::OutputLine& outputLine) override
  {
    const auto outputLineDto = dto::OutputLine::createShared();
    outputLineDto->output = String(outputLine.output.data(), static_cast<v_buff_size>(outputLine.output.size()), false);
    outputLineDto->isErr = outputLine.isErr;
    outputLineDto->file = String(outputLine.fileName.data(), static_cast<v_buff_size>(outputLine.fileName.size()), false);
    outputLineDto->line = outputLine.line;
    
    const auto wrapper = dto::EventMessageWrapper<dto::OutputLine>::createShared();
    wrapper->type = dto::EventMessageType::OutputLine;
    wrapper->message = outputLineDto;

    webSocketInstanceListener_->broadcastMessage(mapper_->writeToString(wrapper));
  }

 private:
  std::shared_ptr<ObjectMapper> mapper_ = ObjectMapper::createShared();
  std::shared_ptr<WSInstanceListener> webSocketInstanceListener_;
};

class EndpointImpl : public EmbeddedServer {

 public:
  explicit EndpointImpl(const ListenerConfig& config)
  {
    appComponents_ = std::make_shared<AppComponents>(config);
  }

  void SetCommandInterface(const std::shared_ptr<MessageCommandInterface> messageCommandInterface) override {

    /* create ApiControllers and add endpoints to router */    
    OATPP_COMPONENT(std::shared_ptr<oatpp::web::server::HttpRouter>, router);
    OATPP_COMPONENT(std::shared_ptr<oatpp::web::server::handler::ErrorHandler>, errorHandler);

    // Controllers

    auto debugCommandController = DebugCommandController::CreateShared(messageCommandInterface);
    debugCommandController->addEndpointsToRouter(router);
    debugCommandController->setErrorHandler(errorHandler);
    controllers_.push_back(debugCommandController);

    auto staticController = StaticController::CreateShared();
    staticController->addEndpointsToRouter(router);
    staticController->setErrorHandler(errorHandler);
    controllers_.push_back(staticController);

    auto websocketController = WebsocketController::CreateShared();
    websocketController->addEndpointsToRouter(router);
    websocketController->setErrorHandler(errorHandler);
    controllers_.push_back(websocketController);

#ifdef SDB_ENABLE_OATPP_SWAGGER
    std::cout << "Using Swagger Resources Path: " << OATPP_SWAGGER_RES_PATH << std::endl;
    std::shared_ptr<oatpp::web::server::api::ApiController::Endpoints> docEndpoints = oatpp::swagger::Controller::Endpoints::createShared();
    docEndpoints->pushBackAll(debugCommandController->getEndpoints());
    docEndpoints->pushBackAll(staticController->getEndpoints());
    docEndpoints->pushBackAll(websocketController->getEndpoints());
    swaggerController_ = oatpp::swagger::Controller::createShared(docEndpoints);
    swaggerController_->addEndpointsToRouter(router);
#endif

    OATPP_COMPONENT(std::shared_ptr<WSInstanceListener>, webSocketInstanceListener);
    eventInterface_ = std::make_shared<OatMessageEventInterface>(webSocketInstanceListener);
  }

  [[nodiscard]] std::shared_ptr<MessageEventInterface> GetEventInterface() const override {
    return eventInterface_;
  }

  void Start() override {
    /* create server */
    worker_ = std::thread([stopping = this->stopping_]() {

      /* Get connection handler component */
      OATPP_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>, connectionHandler, "http");

      /* Get connection provider component */
      OATPP_COMPONENT(std::shared_ptr<oatpp::network::ServerConnectionProvider>, connectionProvider);
      oatpp::network::Server server(connectionProvider, connectionHandler);

      OATPP_LOGD(kTag, "Running on port %s...", connectionProvider->getProperty("port").toString()->c_str())

      server.run([stopping]() { return !*stopping; });

      OATPP_LOGD(kTag, "Stopped")
    });
  }

  // Stops the worker thread, optionally joining the thread until the stop has completed.
  void Stop(const bool join) override {
    *stopping_ = true;
    if (join) {
      worker_.join();
    }
  }

private:
  std::shared_ptr<OatMessageEventInterface> eventInterface_;
  std::shared_ptr<AppComponents> appComponents_;

  // Need to keep a reference to the controllers so they aren't deleted.
  std::vector<std::shared_ptr<oatpp::web::server::api::ApiController>> controllers_;

#ifdef SDB_ENABLE_OATPP_SWAGGER
  std::shared_ptr<oatpp::swagger::Controller> swaggerController_;
#endif

  std::shared_ptr<bool> stopping_ = std::make_shared<bool>(false);
  std::thread worker_;
  static constexpr const char* kTag = "EmbeddedServer";
};

EmbeddedServer* EmbeddedServer::Create(const uint16_t port)
{
  ListenerConfig config;
  config.port = port;
  return new EndpointImpl(config);
}

void EmbeddedServer::InitEnvironment() {
  oatpp::base::Environment::init();
  oatpp::base::Environment::setLogger(std::make_shared<ForwardingLogger>());
}

void EmbeddedServer::ShutdownEnvironment() {
  /* Print how much objects were created during app running, and what have left-probably leaked */
  // TODO: Disable object counting for release builds using '-D OATPP_DISABLE_ENV_OBJECT_COUNTERS' flag for better performance
  {
    std::stringstream ss;
    ss << "\nOATPP Environment:\n";
    ss << "objectsCount = " << oatpp::base::Environment::getObjectsCount() << "\n";
    ss << "objectsCreated = " << oatpp::base::Environment::getObjectsCreated() << "\n\n";
    OATPP_LOGD("Endpoint", ss.str().c_str());
  }

  oatpp::base::Environment::destroy();
}

}// namespace sdb
