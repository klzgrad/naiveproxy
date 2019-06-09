// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/test/launcher/test_launcher.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace {

// Mock TestLauncher to mock CreateAndStartThreadPool,
// unit test will provide a ScopedTaskEnvironment.
class MockTestLauncher : public TestLauncher {
 public:
  MockTestLauncher(TestLauncherDelegate* launcher_delegate,
                   size_t parallel_jobs)
      : TestLauncher(launcher_delegate, parallel_jobs) {}

  void CreateAndStartThreadPool(int parallel_jobs) override {}
};

// Simple TestLauncherDelegate mock to test TestLauncher flow.
class MockTestLauncherDelegate : public TestLauncherDelegate {
 public:
  MOCK_METHOD1(GetTests, bool(std::vector<TestIdentifier>* output));
  MOCK_METHOD2(WillRunTest,
               bool(const std::string& test_case_name,
                    const std::string& test_name));
  MOCK_METHOD2(ShouldRunTest,
               bool(const std::string& test_case_name,
                    const std::string& test_name));
  MOCK_METHOD2(RunTests,
               size_t(TestLauncher* test_launcher,
                      const std::vector<std::string>& test_names));
  MOCK_METHOD2(RetryTests,
               size_t(TestLauncher* test_launcher,
                      const std::vector<std::string>& test_names));
};

// Using MockTestLauncher to test TestLauncher.
// Test TestLauncher filters, and command line switches setup.
class TestLauncherTest : public testing::Test {
 protected:
  TestLauncherTest()
      : command_line(new CommandLine(CommandLine::NO_PROGRAM)),
        test_launcher(&delegate, 10),
        scoped_task_environment(
            base::test::ScopedTaskEnvironment::MainThreadType::IO) {}


  // Adds tests to be returned by the delegate.
  void AddMockedTests(std::string test_case_name,
                      const std::vector<std::string>& test_names) {
    for (const std::string& test_name : test_names) {
      TestIdentifier test_data;
      test_data.test_case_name = test_case_name;
      test_data.test_name = test_name;
      test_data.file = "File";
      test_data.line = 100;
      tests_.push_back(test_data);
    }
  }

  // Setup expected delegate calls, and which tests the delegate will return.
  void SetUpExpectCalls() {
    using ::testing::_;
    EXPECT_CALL(delegate, GetTests(_))
        .WillOnce(::testing::DoAll(testing::SetArgPointee<0>(tests_),
                                   testing::Return(true)));
    EXPECT_CALL(delegate, WillRunTest(_, _))
        .WillRepeatedly(testing::Return(true));
    EXPECT_CALL(delegate, ShouldRunTest(_, _))
        .WillRepeatedly(testing::Return(true));
  }

  std::unique_ptr<CommandLine> command_line;
  MockTestLauncher test_launcher;
  MockTestLauncherDelegate delegate;
  base::test::ScopedTaskEnvironment scoped_task_environment;

 private:
  std::vector<TestIdentifier> tests_;
};

// Action to mock delegate invoking OnTestFinish on test launcher.
ACTION_P2(OnTestResult, full_name, status) {
  TestResult result;
  result.full_name = full_name;
  result.status = status;
  ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      BindOnce(&TestLauncher::OnTestFinished, Unretained(arg0), result));
}

// A test and a disabled test cannot share a name.
TEST_F(TestLauncherTest, TestNameSharedWithDisabledTest) {
  AddMockedTests("Test", {"firstTest", "DISABLED_firstTest"});
  SetUpExpectCalls();
  EXPECT_FALSE(test_launcher.Run(command_line.get()));
}

// A test case and a disabled test case cannot share a name.
TEST_F(TestLauncherTest, TestNameSharedWithDisabledTestCase) {
  AddMockedTests("DISABLED_Test", {"firstTest"});
  AddMockedTests("Test", {"firstTest"});
  SetUpExpectCalls();
  EXPECT_FALSE(test_launcher.Run(command_line.get()));
}

// Compiled tests should not contain an orphaned pre test.
TEST_F(TestLauncherTest, OrphanePreTest) {
  AddMockedTests("Test", {"firstTest", "PRE_firstTestOrphane"});
  SetUpExpectCalls();
  EXPECT_FALSE(test_launcher.Run(command_line.get()));
}

// When There are no tests, RunLoop should not be called.
TEST_F(TestLauncherTest, EmptyTestSetPasses) {
  SetUpExpectCalls();
  using ::testing::_;
  EXPECT_CALL(delegate, RunTests(_, _)).WillOnce(testing::Return(0));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher filters DISABLED tests by default.
TEST_F(TestLauncherTest, FilterDisabledTestByDefault) {
  AddMockedTests("DISABLED_TestDisabled", {"firstTest"});
  AddMockedTests("Test",
                 {"firstTest", "secondTest", "DISABLED_firstTestDisabled"});
  SetUpExpectCalls();
  using ::testing::_;
  std::vector<std::string> tests_names = {"Test.firstTest", "Test.secondTest"};
  EXPECT_CALL(delegate,
              RunTests(_, testing::ElementsAreArray(tests_names.cbegin(),
                                                    tests_names.cend())))
      .WillOnce(::testing::DoAll(
          OnTestResult("Test.firstTest", TestResult::TEST_SUCCESS),
          OnTestResult("Test.secondTest", TestResult::TEST_SUCCESS),
          testing::Return(2)));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher "gtest_filter" switch.
TEST_F(TestLauncherTest, UsingCommandLineFilter) {
  AddMockedTests("Test",
                 {"firstTest", "secondTest", "DISABLED_firstTestDisabled"});
  SetUpExpectCalls();
  command_line->AppendSwitchASCII("gtest_filter", "Test*.first*");
  using ::testing::_;
  std::vector<std::string> tests_names = {"Test.firstTest"};
  EXPECT_CALL(delegate,
              RunTests(_, testing::ElementsAreArray(tests_names.cbegin(),
                                                    tests_names.cend())))
      .WillOnce(::testing::DoAll(
          OnTestResult("Test.firstTest", TestResult::TEST_SUCCESS),
          testing::Return(1)));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher "gtest_repeat" switch.
TEST_F(TestLauncherTest, RunningMultipleIterations) {
  AddMockedTests("Test", {"firstTest"});
  SetUpExpectCalls();
  command_line->AppendSwitchASCII("gtest_repeat", "2");
  using ::testing::_;
  EXPECT_CALL(delegate, RunTests(_, _))
      .WillOnce(::testing::DoAll(
          OnTestResult("Test.firstTest", TestResult::TEST_SUCCESS),
          testing::Return(1)))
      .WillOnce(::testing::DoAll(
          OnTestResult("Test.firstTest", TestResult::TEST_SUCCESS),
          testing::Return(1)));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher will retry failed test, and stop on success.
TEST_F(TestLauncherTest, SuccessOnRetryTests) {
  AddMockedTests("Test", {"firstTest"});
  SetUpExpectCalls();
  using ::testing::_;
  EXPECT_CALL(delegate, RunTests(_, _))
      .WillOnce(::testing::DoAll(
          OnTestResult("Test.firstTest", TestResult::TEST_FAILURE),
          testing::Return(1)));
  std::vector<std::string> tests_names = {"Test.firstTest"};
  EXPECT_CALL(delegate,
              RetryTests(_, testing::ElementsAreArray(tests_names.cbegin(),
                                                      tests_names.cend())))
      .WillOnce(::testing::DoAll(
          OnTestResult("Test.firstTest", TestResult::TEST_SUCCESS),
          testing::Return(1)));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher will retry continuing failing test 3 times by default,
// before eventually failing and returning false.
TEST_F(TestLauncherTest, FailOnRetryTests) {
  AddMockedTests("Test", {"firstTest"});
  SetUpExpectCalls();
  using ::testing::_;
  EXPECT_CALL(delegate, RunTests(_, _))
      .WillOnce(::testing::DoAll(
          OnTestResult("Test.firstTest", TestResult::TEST_FAILURE),
          testing::Return(1)));
  std::vector<std::string> tests_names = {"Test.firstTest"};
  EXPECT_CALL(delegate,
              RetryTests(_, testing::ElementsAreArray(tests_names.cbegin(),
                                                      tests_names.cend())))
      .Times(3)
      .WillRepeatedly(::testing::DoAll(
          OnTestResult("Test.firstTest", TestResult::TEST_FAILURE),
          testing::Return(1)));
  EXPECT_FALSE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher run disabled unit tests switch.
TEST_F(TestLauncherTest, RunDisabledTests) {
  AddMockedTests("DISABLED_TestDisabled", {"firstTest"});
  AddMockedTests("Test",
                 {"firstTest", "secondTest", "DISABLED_firstTestDisabled"});
  SetUpExpectCalls();
  command_line->AppendSwitch("gtest_also_run_disabled_tests");
  command_line->AppendSwitchASCII("gtest_filter", "Test*.first*");
  using ::testing::_;
  std::vector<std::string> tests_names = {"DISABLED_TestDisabled.firstTest",
                                          "Test.firstTest",
                                          "Test.DISABLED_firstTestDisabled"};
  EXPECT_CALL(delegate,
              RunTests(_, testing::ElementsAreArray(tests_names.cbegin(),
                                                    tests_names.cend())))
      .WillOnce(::testing::DoAll(
          OnTestResult("Test.firstTest", TestResult::TEST_SUCCESS),
          OnTestResult("DISABLED_TestDisabled.firstTest",
                       TestResult::TEST_SUCCESS),
          OnTestResult("Test.DISABLED_firstTestDisabled",
                       TestResult::TEST_SUCCESS),
          testing::Return(3)));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Shard index must be lesser than total shards
TEST_F(TestLauncherTest, FaultyShardSetup) {
  command_line->AppendSwitchASCII("test-launcher-total-shards", "2");
  command_line->AppendSwitchASCII("test-launcher-shard-index", "2");
  using ::testing::_;
  std::vector<std::string> tests_names = {"Test.firstTest"};
  EXPECT_FALSE(test_launcher.Run(command_line.get()));
}

// Shard index must be lesser than total shards
TEST_F(TestLauncherTest, RedirectStdio) {
  SetUpExpectCalls();
  command_line->AppendSwitchASCII("test-launcher-print-test-stdio", "always");
  using ::testing::_;
  EXPECT_CALL(delegate, RunTests(_, _))
      .WillOnce(::testing::DoAll(
          OnTestResult("Test.firstTest", TestResult::TEST_SUCCESS),
          testing::Return(1)));
  std::vector<std::string> tests_names = {"Test.firstTest"};
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

}  // namespace

}  // namespace base
