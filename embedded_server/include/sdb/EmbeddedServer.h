//
// Created by Lewis weaver on 5/30/2021.
//

#pragma once

#ifndef EMBEDDED_SERVER_H
#define EMBEDDED_SERVER_H

#include <sdb/MessageInterface.h>

#include <memory>
#include <thread>

namespace sdb {
class EmbeddedServer {
 public:
  virtual ~EmbeddedServer() = default;

  static void InitEnvironment();
  static void ShutdownEnvironment();

  static EmbeddedServer* Create();

  virtual std::shared_ptr<MessageEventInterface> GetEventInterface() const = 0;
  virtual void SetCommandInterface(std::shared_ptr<MessageCommandInterface> messageCommandInterface) = 0;
  virtual void Start() = 0;

  // Stops the worker thread, optionally joining the thread until the stop has completed.
  virtual void Stop(bool join = true) = 0;

  protected: 
    EmbeddedServer() = default;
};
}// namespace sdb

#endif // EMBEDDED_SERVER_H
