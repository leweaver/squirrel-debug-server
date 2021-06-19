//
// Created by Lewis weaver on 5/30/2021.
//
#pragma once

#ifndef SAMPLE_APP_TESTAPPCONTROLLER_H
#define SAMPLE_APP_TESTAPPCONTROLLER_H

#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/parser/json/mapping/ObjectMapper.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/core/macro/component.hpp"
#include "oatpp-websocket/Handshaker.hpp"

#include OATPP_CODEGEN_BEGIN(ApiController)

class TestAppController : public oatpp::web::server::api::ApiController {
 public:
  TestAppController(const std::shared_ptr<ObjectMapper>& objectMapper)
          : oatpp::web::server::api::ApiController(objectMapper)
  {}

  static std::shared_ptr<TestAppController> createShared(
          OATPP_COMPONENT(std::shared_ptr<ObjectMapper>, objectMapper) // Inject objectMapper component here as default parameter
  ){
    return std::make_shared<TestAppController>(objectMapper);
  }

  ENDPOINT("GET", "/", root) {
    const char* html =
            "<html lang='en'>"
            "  <head>"
            "    <meta charset=utf-8/>"
            "  </head>"
            "  <body>"
            "    <p>Hello CRUD example project!</p>"
            "    <a href='swagger/ui'>Checkout Swagger-UI page</a>"
            "  </body>"
            "</html>";
    auto response = createResponse(Status::CODE_200, html);
    response->putHeader(Header::CONTENT_TYPE, "text/html");
    return response;
  }

  ENDPOINT("GET", "ws", ws, REQUEST(std::shared_ptr<IncomingRequest>, request)) {
    return oatpp::websocket::Handshaker::serversideHandshake(request->getHeaders(), websocketConnectionHandler);
  };

 private:
  OATPP_COMPONENT(std::shared_ptr<oatpp::network::ConnectionHandler>, websocketConnectionHandler, "websocket");

};

#include OATPP_CODEGEN_BEGIN(ApiController)

#endif//SAMPLE_APP_TESTAPPCONTROLLER_H
