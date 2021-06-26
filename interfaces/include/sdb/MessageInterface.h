//
// Created by Lewis weaver on 5/31/2021.
//
#pragma once

#ifndef SDB_MESSAGE_INTERFACE_H
#define SDB_MESSAGE_INTERFACE_H

#include <cinttypes>
#include <string>
#include <vector>

namespace sdb {
namespace data {
enum class ReturnCode {
  // Everything was all good
  Success = 0,

  // Invalid means user input to a call caused the call to fail
  Invalid = 100,
  InvalidNotPaused = 101,
  InvalidParameter = 102,

  // Error means something went wrong inside the implementation, not good.
  ErrorInternal = 200
};
enum class Runstate { Running = 0, Pausing = 1, Paused = 2, Stepping = 3 };
struct StackEntry {
  std::string file;
  int64_t line;
  std::string function;
};
struct Status {
  Runstate runstate;
  std::vector<StackEntry> stack;
};
enum class VariableType { String, Bool, Integer, Float, Closure, Class, Instance, Array, Table, Other };
struct Variable {
  std::string name;
  VariableType type;
  std::string value;
  uint32_t childCount;
};
struct PaginationInfo {
  uint32_t beginIndex;
  uint32_t count;
};
}// namespace data

/// <summary>
/// Interface that is used to communicate commands from the remote debugger
/// Implemention is provided by the application.
/// </summary>
class MessageCommandInterface {
 public:
  /// <summary>
  /// Instructs the program to pause execution at its current point
  /// </summary>
  [[nodiscard]] virtual data::ReturnCode Pause() = 0;

  /// <summary>
  /// Instructs the program to resume execution if it was previously paused
  /// </summary>
  [[nodiscard]] virtual data::ReturnCode Continue() = 0;

  /// <summary>
  /// Instructs the program to execute until it pops 1 level up the stack if it was previously paused
  /// </summary>
  [[nodiscard]] virtual data::ReturnCode StepOut() = 0;

  /// <summary>
  /// Instructs the program to execute until it reaches another line at this stack level
  /// </summary>
  [[nodiscard]] virtual data::ReturnCode StepOver() = 0;

  /// <summary>
  /// Instructs the program to execute a single step if it was previously paused
  /// </summary>
  [[nodiscard]] virtual data::ReturnCode StepIn() = 0;

  /// <summary>
  /// Instructs the program to send out current state: ie playing or paused.
  /// </summary>
  [[nodiscard]] virtual data::ReturnCode SendStatus() = 0;

  [[nodiscard]] virtual data::ReturnCode GetStackLocals(int32_t stackFrame, const std::string& path,
                                                        const data::PaginationInfo& pagination,
                                                        std::vector<data::Variable>& variables) = 0;
};

/// <summary>
/// Interface that is used to communicate state from the app to the remote debugger.
/// Implemention is provided by the remote debugger implementation.
/// </summary>
class MessageEventInterface {
 public:
  virtual void OnStatus(data::Status&& status) = 0;
};
}// namespace sdb

#endif
