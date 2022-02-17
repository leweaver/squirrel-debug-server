
#ifndef SWAGGER_COMPONENT_H
#define SWAGGER_COMPONENT_H

#ifdef SDB_ENABLE_OATPP_SWAGGER
#include "oatpp-swagger/Model.hpp"
#include "oatpp-swagger/Resources.hpp"

#include <sstream>
#endif

#include "oatpp/core/macro/component.hpp"

#include "ListenerConfig.h"

namespace sdb {
#ifdef SDB_ENABLE_OATPP_SWAGGER
/**
 *  Swagger ui is served at
 *  http://host:port/swagger/ui
 */
class SwaggerComponent {
 public:
  explicit SwaggerComponent(const ListenerConfig& config)
      : swaggerDocumentInfo(CreateDocumentInfo(config))
  {}

  /**
   *  General API docs info
   */
  oatpp::base::Environment::Component<std::shared_ptr<oatpp::swagger::DocumentInfo>> swaggerDocumentInfo;
  static std::shared_ptr<oatpp::swagger::DocumentInfo> CreateDocumentInfo(const ListenerConfig& listenerConfig)
  {
    oatpp::swagger::DocumentInfo::Builder builder;

    std::stringstream ss;
    ss << "http://" << listenerConfig.hostName << ":" << listenerConfig.port;

    builder.setTitle("Squirrel Remote Debugging")
            .setDescription("HTTP command and WebSocket event interface to Squirrel Debugger.")
            .setVersion("1.0")
            .setContactName("Lewis Weaver")
            // TODO: Set github URL
            .setContactUrl("")

            // TODO: Set MIT license
            .setLicenseName("Apache License, Version 2.0")
            .setLicenseUrl("http://www.apache.org/licenses/LICENSE-2.0")
            .addServer(ss.str().c_str(), ("server on " + listenerConfig.hostName).c_str());

    return builder.build();
  }


  /**
   *  Swagger-Ui Resources (<oatpp-examples>/lib/oatpp-swagger/res)
   */
  OATPP_CREATE_COMPONENT(std::shared_ptr<oatpp::swagger::Resources>, swaggerResources)
  ([] { return oatpp::swagger::Resources::loadResources(OATPP_SWAGGER_RES_PATH); }());
};
#else
class SwaggerComponent {
 public:
  explicit SwaggerComponent(const ListenerConfig& config)
  {}
};
#endif
}// namespace sdb

#endif /* SWAGGER_COMPONENT_H */