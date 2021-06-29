//
// Created by Lewis weaver on 5/30/2021.
//
#pragma once

#ifndef STATIC_CONTROLLER_H
#define STATIC_CONTROLLER_H

#include <oatpp/core/macro/codegen.hpp>
#include <oatpp/core/macro/component.hpp>
#include <oatpp/parser/json/mapping/ObjectMapper.hpp>
#include <oatpp/web/server/api/ApiController.hpp>

#include OATPP_CODEGEN_BEGIN(ApiController)

namespace sdb {
class StaticController final : public oatpp::web::server::api::ApiController {
 public:
  explicit StaticController(const std::shared_ptr<ObjectMapper>& objectMapper) : ApiController(objectMapper) {}

  static std::shared_ptr<StaticController>
  CreateShared(OATPP_COMPONENT(std::shared_ptr<ObjectMapper>, objectMapper)
  ) {
    return std::make_shared<StaticController>(objectMapper);
  }

  ENDPOINT("GET", "/", Root) {
    const char* html = "<html lang='en'>"
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
};
}// namespace sdb

#include OATPP_CODEGEN_BEGIN(ApiController)
#endif//STATIC_CONTROLLER_H
