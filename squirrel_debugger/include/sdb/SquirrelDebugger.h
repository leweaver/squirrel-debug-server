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
struct PauseMutexDataImpl;
struct SquirrelVmDataImpl;

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
  [[nodiscard]] data::ReturnCode pauseExecution() override;
  [[nodiscard]] data::ReturnCode continueExecution() override;
  [[nodiscard]] data::ReturnCode stepOut() override;
  [[nodiscard]] data::ReturnCode stepOver() override;
  [[nodiscard]] data::ReturnCode stepIn() override;
  [[nodiscard]] data::ReturnCode sendStatus() override;
  [[nodiscard]] data::ReturnCode getStackVariables(int32_t stackFrame, const std::string& path,
                                                   const data::PaginationInfo& pagination,
                                                   std::vector<data::Variable>& variables) override;

  [[nodiscard]] data::ReturnCode getGlobalVariables(const std::string& path, const data::PaginationInfo& pagination,
                                                    std::vector<data::Variable>& variables) override;

  [[nodiscard]] data::ReturnCode setFileBreakpoints(const std::string& file,
                                                    const std::vector<data::CreateBreakpoint>& createBps,
                                                    std::vector<data::ResolvedBreakpoint>& resolvedBps) override;

  // The following methods are called from the scripting engine thread
  void setEventInterface(std::shared_ptr<sdb::MessageEventInterface> eventInterface);
  void addVm(HSQUIRRELVM vm);
  void squirrelNativeDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourceName, SQInteger line,
                               const SQChar* functionName);

 private:
  enum class PauseType : uint8_t { None, StepOut, StepOver, StepIn, Pause = StepIn };
  [[nodiscard]] data::ReturnCode Step(PauseType pauseType, int returnsRequired);

  std::shared_ptr<MessageEventInterface> eventInterface_;

  // Pause Mechanism. First a pause is requested, then it is confirmed. We can only safely
  // read Squirrel state once the pause is confirmed, as it means that the scripting engine
  // is no longer executing.
  std::atomic<PauseType> pauseRequested_ = PauseType::None;

  // must lock pauseMutex_ to modify any members of this struct instance
  PauseMutexDataImpl* pauseMutexData_;

  std::mutex pauseMutex_;
  std::condition_variable pauseCv_;

  // Incremented whenever breakpoints are modified.
  std::atomic_uint64_t breakpointMapChangeCount_;

  // This must only be accessed within the Squirrel Execution Thread.
  SquirrelVmDataImpl* vmData_;
};
}// namespace sdb

#endif// SQUIRREL_DEBUGGER_H