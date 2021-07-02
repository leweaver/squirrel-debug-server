//
// Created by Lewis weaver on 5/30/2021.
//

#pragma once

#ifndef EMBEDDED_SERVER_H
#define EMBEDDED_SERVER_H

#include <sdb/MessageInterface.h>

#include <memory>

namespace sdb {
class EmbeddedServer {
 public:
  virtual ~EmbeddedServer() = default;

  // Deleted methods
  EmbeddedServer(const EmbeddedServer& other) = delete;
  EmbeddedServer(const EmbeddedServer&& other) = delete;
  EmbeddedServer& operator=(const EmbeddedServer&) = delete;
  EmbeddedServer& operator=(EmbeddedServer&&) = delete;

  // Must be called once at application startup/teardown
  static void InitEnvironment();
  static void ShutdownEnvironment();

  // Creates a new instance. Should be called after the environment has been initialized.
  [[nodiscard]] static EmbeddedServer* Create(uint16_t port);

  [[nodiscard]] virtual std::shared_ptr<MessageEventInterface> GetEventInterface() const = 0;
  virtual void SetCommandInterface(std::shared_ptr<MessageCommandInterface> messageCommandInterface) = 0;
  virtual void Start() = 0;

  /**
   * Stops the worker thread, optionally joining the thread until the stop has completed.
   * @param join - if true, will join the worker thread (wait for it to complete)
   */
  virtual void Stop(bool join = true) = 0;

  protected: 
    EmbeddedServer() = default;
};
}// namespace sdb

#endif // EMBEDDED_SERVER_H
