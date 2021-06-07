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
  void Play() override;
  void SendStatus() override;

  // The following methods are called from the scripting engine thread
  void SetEventInterface(std::shared_ptr<qdb::MessageEventInterface> eventInterface);
  void SetVm(HSQUIRRELVM vm);
  void SquirrelNativeDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line, const SQChar* funcname);

 private:
  HSQUIRRELVM vm_;
  std::shared_ptr<qdb::MessageEventInterface> eventInterface_;

  // Pause Mechanism. First a pause is requested, then it is confirmed. We can only safely
  // read Squirrel state once the pause is confirmed, as it means that the scripting engine
  // is no longer executing.
  std::atomic_bool pauseRequested_ = false;
  std::atomic_bool isPaused_ = false;
  std::mutex pauseMutex_;
  std::condition_variable pauseCv_;

  // The status as it was last time the application paused.
  qdb::data::Status status_ = {};
};

#endif// SAMPLE_APP_QUIRREL_DEBUGGER_H