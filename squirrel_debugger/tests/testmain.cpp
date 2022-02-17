#include "DebuggerTestUtils.h"

#include <sdb/SquirrelDebugger.h>

#include <array>
#include <thread>

using sdb::SquirrelDebugger;
using sdb::data::ReturnCode;
using sdb::data::RunState;

namespace sdb::tests {
class SquirrelDebuggerVariablesTest : public SquirrelDebuggerTest {
 public:
  // Fixtures
  static constexpr const char* kTestFileName = "test.nut";
  static constexpr int kBpLineNumber = 58;// nb - line numbers start at 1
  static constexpr int kBpId = 4322;
  static constexpr const char* kStrExpValue = "string expr";
  static constexpr const char* kv0XValue = "1";
};

TEST_F(SquirrelDebuggerVariablesTest, GetLocalVariableTest)
{
  RunAndPauseTestFile(kTestFileName);

  std::vector<sdb::data::CreateBreakpoint> createBps;
  createBps.push_back({kBpId, kBpLineNumber});
  std::vector<sdb::data::ResolvedBreakpoint> resolvedBps;
  ASSERT_EQ(ReturnCode::Success, GetDebugger().SetFileBreakpoints(kTestFileName, createBps, resolvedBps));
  ASSERT_EQ(resolvedBps.size(), 1);
  ASSERT_EQ(resolvedBps.at(0).id, kBpId);
  ASSERT_EQ(resolvedBps.at(0).line, kBpLineNumber);

  ResetWaitForStatus();
  ASSERT_EQ(ReturnCode::Success, GetDebugger().ContinueExecution());
  WaitForStatus(RunState::Paused);

  // Validate that the thread paused at the right spot
  sdb::data::Status status;
  GetLastStatus(status);
  ASSERT_EQ(status.pausedAtBreakpointId, kBpId);
  ASSERT_FALSE(status.stack.empty());
  ASSERT_EQ(status.stack[0].line, kBpLineNumber);

  // Check local variable
  std::vector<sdb::data::Variable> variables;
  ASSERT_EQ(ReturnCode::Success, GetDebugger().GetStackVariables(0, "", kPagination, variables));

  auto pos = std::find_if(variables.begin(), variables.end(), [](const sdb::data::Variable& var) {
    return var.pathUiString == "strExp";
  });
  ASSERT_NE(pos, variables.end());
  ASSERT_EQ(pos->value, kStrExpValue);
}

TEST_F(SquirrelDebuggerVariablesTest, SetStackStringVariableTest)
{
  RunAndPauseTestFileAtLine(kTestFileName, {kBpId, kBpLineNumber});

  // Check root local variable
  std::vector<sdb::data::Variable> variables;
  ASSERT_EQ(ReturnCode::Success, GetDebugger().GetStackVariables(0, "", kPagination, variables));

  //////////
  // Local string variable
  auto strExpPos = std::find_if(variables.begin(), variables.end(), [](const sdb::data::Variable& var) {
    return var.pathUiString == "strExp";
  });
  ASSERT_NE(strExpPos, variables.end());
  ASSERT_EQ(strExpPos->value, kStrExpValue);
  ASSERT_EQ(strExpPos->editable, false);

  // Attempt to set the new value - this should fail, can't set top level variable values.
  {
    std::stringstream ssPathIter;
    ssPathIter << strExpPos->pathIterator;
    const std::string newValueString = "new value";
    sdb::data::Variable newValueOut;
    ASSERT_EQ(
            ReturnCode::InvalidParameter,
            GetDebugger().SetStackVariableValue(0, ssPathIter.str(), newValueString, newValueOut));
  }
}

TEST_F(SquirrelDebuggerVariablesTest, SetStackInstanceVariableTest)
{
  RunAndPauseTestFileAtLine(kTestFileName, {kBpId, kBpLineNumber});

  // Check root local variable
  std::vector<sdb::data::Variable> variables;
  ASSERT_EQ(ReturnCode::Success, GetDebugger().GetStackVariables(0, "", kPagination, variables));

  //////////
  // Local class instance variable
  auto v0Pos = std::find_if(variables.begin(), variables.end(), [](const sdb::data::Variable& var) {
    return var.pathUiString == "v0";
  });
  ASSERT_NE(v0Pos, variables.end());

  std::vector<sdb::data::Variable> v0variables;
  std::string v0path = std::to_string(v0Pos->pathIterator);
  ASSERT_EQ(ReturnCode::Success, GetDebugger().GetStackVariables(0, v0path, kPagination, v0variables));
  ASSERT_EQ(v0Pos->childCount, 5);
  ASSERT_EQ(v0variables.size(), 5);
  // Will be sorted: Class methods/fields sorted a-z, then Parent class methods/fields sorted a-z
  ASSERT_EQ(v0variables[0].pathUiString, "Print");
  ASSERT_EQ(v0variables[1].pathUiString, "constructor");
  ASSERT_EQ(v0variables[2].pathUiString, "x");
  ASSERT_EQ(v0variables[3].pathUiString, "y");
  ASSERT_EQ(v0variables[4].pathUiString, "z");

  const std::string newValueString = "99";
  sdb::data::Variable newValueOut;

  // Can set v0.x as it is a child variable.
  {
    ASSERT_EQ(v0variables[2].editable, true);
    std::string v0xpath = v0path + SquirrelDebugger::kPathSeparator + std::to_string(v0variables[2].pathIterator);
    ASSERT_EQ(ReturnCode::Success, GetDebugger().SetStackVariableValue(0, v0xpath, newValueString, newValueOut));
    ASSERT_EQ(newValueOut.value, newValueString);
  }

  // Can't set v0.Print, as it's current value is not a primitive type.
  {
    ASSERT_EQ(v0variables[0].editable, false);
    std::string v0Printpath = v0path + SquirrelDebugger::kPathSeparator + std::to_string(v0variables[0].pathIterator);
    ASSERT_EQ(ReturnCode::InvalidParameter, GetDebugger().SetStackVariableValue(0, v0Printpath, newValueString, newValueOut));
  }

  // Can't set v0 as it's a local variable on the current closure.
  {
    ASSERT_EQ(v0Pos->editable, false);
    ASSERT_EQ(ReturnCode::InvalidParameter, GetDebugger().SetStackVariableValue(0, v0path, newValueString, newValueOut));
  }
}
}// namespace sdb::tests