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
class SquirrelDebugger : public sdb::MessageCommandInterface {
 public:
  // The following methods are called from the networking thread.
  [[nodiscard]] data::ReturnCode Pause() override;
  [[nodiscard]] data::ReturnCode Continue() override;
  [[nodiscard]] data::ReturnCode StepOut() override;
  [[nodiscard]] data::ReturnCode StepOver() override;
  [[nodiscard]] data::ReturnCode StepIn() override;
  [[nodiscard]] data::ReturnCode SendStatus() override;
  [[nodiscard]] data::ReturnCode GetStackLocals(int32_t stackFrame, const std::string& path, std::vector<data::Variable>& variables) override;

  // The following methods are called from the scripting engine thread
  void SetEventInterface(std::shared_ptr<sdb::MessageEventInterface> eventInterface);
  void SetVm(HSQUIRRELVM vm);
  void SquirrelNativeDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line, const SQChar* funcname);

 private:
  enum class PauseType : uint8_t {
    None,
    StepOut,
    StepOver,
    StepIn,
    Pause = StepIn
  };
  [[nodiscard]] data::ReturnCode Step(PauseType pauseType, int returnsRequired);

  std::shared_ptr<sdb::MessageEventInterface> eventInterface_;

  // Pause Mechanism. First a pause is requested, then it is confirmed. We can only safely
  // read Squirrel state once the pause is confirmed, as it means that the scripting engine
  // is no longer executing.
  std::atomic<PauseType> pauseRequested_ = PauseType::None;
  // must lock pauseMutex_ to modify any members of this struct instance
  struct PauseMutexData {
    bool isPaused = false;

    // How many levels of the stack must be popped before we break
    int returnsRequired = 0;

    // The status as it was last time the application paused.
    sdb::data::Status status = {};
  } pauseMutexData_ = {};
  std::mutex pauseMutex_;
  std::condition_variable pauseCv_;

  // This must only be accesed within the Squirrel Execution Thread.
  struct SquirrelVmData {
    void PopulateStack(std::vector<sdb::data::StackEntry>& stack) const;
    void PopulateStackVariables(std::vector<sdb::data::StackEntry>& stack) const;

    int currentStackDepth = 0;
    HSQUIRRELVM vm = nullptr;
  } vmData_;
};
}// namespace sdb

#endif// SAMPLE_APP_QUIRREL_DEBUGGER_H