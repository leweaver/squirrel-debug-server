
#ifndef SWAGGER_COMPONENT_H
#define SWAGGER_COMPONENT_H

#include "oatpp-swagger/Model.hpp"
#include "oatpp-swagger/Resources.hpp"
#include "oatpp/core/macro/component.hpp"

namespace sdb {
/**
 *  Swagger ui is served at
 *  http://host:port/swagger/ui
 */
class SwaggerComponent {
 public:
  /**
   *  General API docs info
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::swagger::DocumentInfo>, swaggerDocumentInfo)
  ([] {
    oatpp::swagger::DocumentInfo::Builder builder;

    builder
            .setTitle("Squirrel Remote Debugging")
            .setDescription("HTTP command and WebSocket event interface to Squirrel Debugger.")
            .setVersion("1.0")
            .setContactName("Lewis Weaver")
             // TODO: Set github URL
            .setContactUrl("")

            // TODO: Set MIT license
            .setLicenseName("Apache License, Version 2.0")
            .setLicenseUrl("http://www.apache.org/licenses/LICENSE-2.0")

            // TODO: More intelligently set server and port
            .addServer("http://localhost:8000", "server on localhost");

    return builder.build();
  }());


  /**
   *  Swagger-Ui Resources (<oatpp-examples>/lib/oatpp-swagger/res)
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::swagger::Resources>, swaggerResources)
  ([] {
    return oatpp::swagger::Resources::loadResources(OATPP_SWAGGER_RES_PATH);
  }());
};
}// namespace sdb

#endif /* SWAGGER_COMPONENT_H */