//
// Created by Lewis weaver on 5/31/2021.
//
#pragma once

#ifndef SDB_MESSAGE_INTERFACE_H
#define SDB_MESSAGE_INTERFACE_H

#include <cinttypes>
#include <memory>
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
enum class RunState { Running = 0, Pausing = 1, Paused = 2, Stepping = 3 };
struct StackEntry {
  std::string file;
  uint32_t line;
  std::string function;
};
struct Status {
  RunState runState = RunState::Paused;
  std::vector<StackEntry> stack;
  uint64_t pausedAtBreakpointId = 0;
};

struct OutputLine
{
  std::string_view const output;
  bool isErr = false;
  std::string_view const fileName;
  uint32_t line;
};
enum class VariableType {
  Null,
  Integer,
  Float,
  Bool,
  String,
  Table,
  Array,
  UserData,
  Closure,
  NativeClosure,
  Generator,
  UserPointer,
  Thread,
  FuncProto,
  Class,
  Instance,
  WeakRef,
  Outer
};

struct Variable {
  uint64_t pathIterator = 0;
  std::string pathUiString;
  VariableType pathTableKeyType = VariableType::Null;
  VariableType valueType = VariableType::Null;
  std::string value;
  uint64_t valueRawAddress = 0;
  uint32_t childCount = 0;
  // If valueType is Instance, this is set with the full class name.
  std::string instanceClassName;
};
struct PaginationInfo {
  uint32_t beginIterator;
  uint32_t count;
};
struct CreateBreakpoint {
  // ID must be >= 1
  uint64_t id;
  // Line must be >= 1
  uint32_t line;
};
struct ResolvedBreakpoint {
  uint64_t id;
  uint32_t line;
  bool verified;
};
}// namespace data

/// <summary>
/// Interface that is used to communicate commands from the remote debugger
/// Implementation is provided by the application.
/// </summary>
class MessageCommandInterface {
 public:
  MessageCommandInterface() = default;
  virtual ~MessageCommandInterface() = default;

  // Deleted methods
  MessageCommandInterface(const MessageCommandInterface& other) = delete;
  MessageCommandInterface(const MessageCommandInterface&& other) = delete;
  MessageCommandInterface& operator=(const MessageCommandInterface&) = delete;
  MessageCommandInterface& operator=(MessageCommandInterface&&) = delete;

  // Interface definition

  /// <summary> 
  /// Instructs the program to pause execution at its current point
  /// </summary>
  [[nodiscard]] virtual data::ReturnCode PauseExecution() = 0;

  /// <summary>
  /// Instructs the program to resume execution if it was previously paused
  /// </summary>
  [[nodiscard]] virtual data::ReturnCode ContinueExecution() = 0;

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

  [[nodiscard]] virtual data::ReturnCode GetStackVariables(uint32_t stackFrame, const std::string& path,
                                                           const data::PaginationInfo& pagination,
                                                           std::vector<data::Variable>& variables) = 0;

  [[nodiscard]] virtual data::ReturnCode GetGlobalVariables(const std::string& path,
                                                            const data::PaginationInfo& pagination,
                                                            std::vector<data::Variable>& variables) = 0;

  [[nodiscard]] virtual data::ReturnCode SetFileBreakpoints(const std::string& file,
                                                            const std::vector<data::CreateBreakpoint>& createBps,
                                                            std::vector<data::ResolvedBreakpoint>& resolvedBps) = 0;
};

/// <summary>
/// Interface that is used to communicate state from the app to the remote debugger.
/// Implementation is provided by the remote debugger implementation.
/// </summary>
class MessageEventInterface {
 public:
  MessageEventInterface() = default;
  virtual ~MessageEventInterface() = default;

  // Deleted methods
  MessageEventInterface(const MessageEventInterface& other) = delete;
  MessageEventInterface(const MessageEventInterface&& other) = delete;
  MessageEventInterface& operator=(const MessageEventInterface&) = delete;
  MessageEventInterface& operator=(MessageEventInterface&&) = delete;

  // Interface definition
  virtual void HandleStatusChanged(const data::Status& status) = 0;
  virtual void HandleOutputLine(const data::OutputLine& outputLine) = 0;
};
}// namespace sdb

#endif
