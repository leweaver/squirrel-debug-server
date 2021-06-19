//
// Created by Lewis weaver on 5/31/2021.
//
#pragma once

#ifndef SAMPLE_APP_QUIRREL_DEBUGGER_H
#define SAMPLE_APP_QUIRREL_DEBUGGER_H

#include "../debug-server/MessageInterface.h"

#include <squirrel.h>

#include <atomic>
#include <mutex>
#include <condition_variable>

class QuirrelDebugger : public qdb::MessageCommandInterface {
 public:

   // The following methods are called from the networking thread.
  void Pause() override;
  void Continue() override;
  void StepOut() override;
  void StepOver() override;
  void StepIn() override;
  void SendStatus() override;

  // The following methods are called from the scripting engine thread
  void SetEventInterface(std::shared_ptr<qdb::MessageEventInterface> eventInterface);
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
  void Step(PauseType pauseType, int returnsRequired);

  std::shared_ptr<qdb::MessageEventInterface> eventInterface_;

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
    qdb::data::Status status = {};
  } pauseMutexData_ = {};
  std::mutex pauseMutex_;
  std::condition_variable pauseCv_;

  // This must only be accesed within the Squirrel Execution Thread.
  struct SquirrelVmData {
    void PopulateStack(std::vector<qdb::data::StackEntry>& stack) const;
    void PopulateStackVariables(std::vector<qdb::data::StackEntry>& stack) const;

    int currentStackDepth = 0;
    HSQUIRRELVM vm = nullptr;
  } vmData_;
};

#endif// SAMPLE_APP_QUIRREL_DEBUGGER_H