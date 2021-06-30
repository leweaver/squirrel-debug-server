//
// Created by Lewis weaver
//
#pragma once

#ifndef REQUEST_ERROR_HANDLER_H
#define REQUEST_ERROR_HANDLER_H

#include <oatpp/core/base/Environment.hpp>
#include <oatpp/web/server/handler/ErrorHandler.hpp>

namespace sdb {
class RequestErrorHandler : public oatpp::web::server::handler::DefaultErrorHandler {
 public:
  static std::shared_ptr<RequestErrorHandler> CreateShared()
  {
    return std::make_shared<RequestErrorHandler>();
  }

  std::shared_ptr<oatpp::web::protocol::http::outgoing::Response> handleError(
          const oatpp::web::protocol::http::Status& status, const oatpp::String& message,
          const Headers& headers) override
  {
    if (status.code >= 500) {
      OATPP_LOGW("RequestErrorHandler", "HTTP %d: %s", status.code, message->c_str())
    }
    else {
      OATPP_LOGD("RequestErrorHandler", "HTTP %d: %s", status.code, message->c_str())
    }
    return DefaultErrorHandler::handleError(status, message, headers);
  }
};
}// namespace sdb

#endif//REQUEST_ERROR_HANDLER_H
