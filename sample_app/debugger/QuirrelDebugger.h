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
  void Pause() override;
  void Play() override;

  void SquirrelNativeDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line, const SQChar* funcname);

 protected:
  std::atomic_bool paused_ = false;
  std::mutex pauseMutex_;
  std::condition_variable pauseCv_;
};

#endif// SAMPLE_APP_QUIRREL_DEBUGGER_H