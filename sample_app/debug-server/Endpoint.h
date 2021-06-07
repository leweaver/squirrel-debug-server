//
// Created by Lewis weaver on 5/30/2021.
//

#pragma once

#ifndef SAMPLE_APP_ENDPOINT_H
#define SAMPLE_APP_ENDPOINT_H

#include "MessageInterface.h"

#include <memory>
#include <thread>

namespace qdb {
class Endpoint {
 public:
  virtual ~Endpoint() = default;

  static void InitEnvironment();
  static void ShutdownEnvironment();

  static Endpoint* Create();

  virtual std::shared_ptr<MessageEventInterface> GetEventInterface() const = 0;
  virtual void SetCommandInterface(std::shared_ptr<MessageCommandInterface> messageCommandInterface) = 0;
  virtual void Start() = 0;

  // Stops the worker thread, optionally joining the thread until the stop has completed.
  virtual void Stop(bool join = true) = 0;

  protected: 
    Endpoint() = default;
};
}// namespace qdb

#endif//SAMPLE_APP_ENDPOINT_H
