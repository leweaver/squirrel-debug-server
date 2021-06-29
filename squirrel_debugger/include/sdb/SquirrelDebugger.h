//
// Created by Lewis weaver on 5/31/2021.
//
#pragma once

#ifndef SQUIRREL_DEBUGGER_H
#define SQUIRREL_DEBUGGER_H

#include <sdb/MessageInterface.h>

#include <squirrel.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace sdb {
namespace internal {
struct PauseMutexDataImpl;
struct SquirrelVmDataImpl;
}// namespace internal

class SquirrelDebugger final : public MessageCommandInterface {
 public:
  SquirrelDebugger();
  ~SquirrelDebugger() override;

  // Deleted methods
  SquirrelDebugger(const SquirrelDebugger& other) = delete;
  SquirrelDebugger(const SquirrelDebugger&& other) = delete;
  SquirrelDebugger& operator=(const SquirrelDebugger&) = delete;
  SquirrelDebugger& operator=(SquirrelDebugger&&) = delete;

  // The following methods are called from the networking thread.
  [[nodiscard]] data::ReturnCode PauseExecution() override;
  [[nodiscard]] data::ReturnCode ContinueExecution() override;
  [[nodiscard]] data::ReturnCode StepOut() override;
  [[nodiscard]] data::ReturnCode StepOver() override;
  [[nodiscard]] data::ReturnCode StepIn() override;
  [[nodiscard]] data::ReturnCode SendStatus() override;
  [[nodiscard]] data::ReturnCode GetStackVariables(int32_t stackFrame, const std::string& path,
                                                   const data::PaginationInfo& pagination,
                                                   std::vector<data::Variable>& variables) override;

  [[nodiscard]] data::ReturnCode GetGlobalVariables(const std::string& path, const data::PaginationInfo& pagination,
                                                    std::vector<data::Variable>& variables) override;

  [[nodiscard]] data::ReturnCode SetFileBreakpoints(const std::string& file,
                                                    const std::vector<data::CreateBreakpoint>& createBps,
                                                    std::vector<data::ResolvedBreakpoint>& resolvedBps) override;

  // The following methods are called from the scripting engine thread
  void SetEventInterface(std::shared_ptr<MessageEventInterface> eventInterface);
  void AddVm(HSQUIRRELVM vm);
  void SquirrelNativeDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourceName, SQInteger line,
                               const SQChar* functionName);

  // Configuration
  static SQInteger DefaultStackSize();

 private:
  enum class PauseType : uint8_t { None, StepOut, StepOver, StepIn, Pause = StepIn };
  [[nodiscard]] data::ReturnCode Step(PauseType pauseType, int returnsRequired);

  std::shared_ptr<MessageEventInterface> eventInterface_;

  // Pause Mechanism. First a pause is requested, then it is confirmed. We can only safely
  // read Squirrel state once the pause is confirmed, as it means that the scripting engine
  // is no longer executing.
  std::atomic<PauseType> pauseRequested_ = PauseType::None;

  // must lock pauseMutex_ to modify any members of this struct instance
  internal::PauseMutexDataImpl* pauseMutexData_;

  std::mutex pauseMutex_;
  std::condition_variable pauseCv_;

  // Incremented whenever breakpoints are modified.
  std::atomic_uint64_t breakpointMapChangeCount_;

  // This must only be accessed within the Squirrel Execution Thread.
  internal::SquirrelVmDataImpl* vmData_;
};
}// namespace sdb

#endif// SQUIRREL_DEBUGGER_H