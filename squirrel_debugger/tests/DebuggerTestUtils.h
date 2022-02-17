//
// Created by Lewis weaver on 5/31/2021.
//
#pragma once

#ifndef SDB_SQUIRREL_DEBUGGER_DEBUGGERTESTUTILS_H
#define SDB_SQUIRREL_DEBUGGER_DEBUGGERTESTUTILS_H

#include "gtest/gtest.h"

#include <sdb/LogInterface.h>
#include <sdb/SquirrelDebugger.h>

#include <cstdarg>
#include <array>
#include <thread>

namespace sdb::tests {

class MessageEventInterfaceImpl;

class SquirrelDebuggerTest : public testing::Test
{
 public:

  static constexpr const sdb::data::PaginationInfo kPagination {0,100};

  static SquirrelDebuggerTest& Instance() { return *gInstance; }

  void HandleOutputLine(SQVM* vm, bool isErr, const SQChar* text, char* args) const;

  void HandleDebugHook(SQVM* v, SQInteger type, const SQChar* sourceName, SQInteger line,
                       const SQChar* funcName);

 protected:
  void SetUp() override;

  void TearDown() override;

  void CreateVm();

  void RunAndPauseTestFile(const char* testFileName);

  void RunAndPauseTestFileAtLine(const char* testFileName, const sdb::data::CreateBreakpoint& bp);

  SquirrelDebugger& GetDebugger();

  void ResetWaitForStatus();

  bool WaitForStatus(sdb::data::RunState runState);

  void GetLastStatus(sdb::data::Status& status);

 private:

  std::unique_ptr<SquirrelDebugger> debugger_;
  HSQUIRRELVM vm_ = nullptr;
  static SquirrelDebuggerTest* gInstance;

  std::shared_ptr<MessageEventInterfaceImpl> eventInterface_;
  std::thread squirrelWorker_;
};


}


#endif//SDB_SQUIRREL_DEBUGGER_DEBUGGERTESTUTILS_H
