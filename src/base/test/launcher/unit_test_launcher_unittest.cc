// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/test/gtest_util.h"
#include "base/test/launcher/test_launcher.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/scoped_task_environment.h"
#include "base/test/test_switches.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace {

// Unit tests to validate DefaultUnitTestPlatformDelegate implementations.
class DefaultUnitTestPlatformDelegateTester : public testing::Test {
 protected:
  UnitTestPlatformDelegate* platformDelegate;
  FilePath flag_path;
  FilePath output_path;
  std::vector<std::string> test_names;

  void SetUp() override { platformDelegate = &defaultPlatform_; }

 private:
  DefaultUnitTestPlatformDelegate defaultPlatform_;
};

// Call fails when flag_file does not exist.
TEST_F(DefaultUnitTestPlatformDelegateTester, FlagPathCheckFail) {
  ASSERT_CHECK_DEATH(platformDelegate->GetCommandLineForChildGTestProcess(
      test_names, output_path, flag_path));
}

// Validate flags are set correctly in by the delegate.
TEST_F(DefaultUnitTestPlatformDelegateTester,
       GetCommandLineForChildGTestProcess) {
  ASSERT_TRUE(platformDelegate->CreateResultsFile(&output_path));
  ASSERT_TRUE(platformDelegate->CreateTemporaryFile(&flag_path));
  CommandLine cmd_line(platformDelegate->GetCommandLineForChildGTestProcess(
      test_names, output_path, flag_path));
  EXPECT_EQ(cmd_line.GetSwitchValueASCII("test-launcher-output"),
            output_path.MaybeAsASCII());
  EXPECT_EQ(cmd_line.GetSwitchValueASCII("gtest_flagfile"),
            flag_path.MaybeAsASCII());
  EXPECT_TRUE(cmd_line.HasSwitch("single-process-tests"));
}

// Validate the tests are saved correctly in flag file under
// the "--gtest_filter" flag.
TEST_F(DefaultUnitTestPlatformDelegateTester, GetCommandLineFilterTest) {
  test_names.push_back("Test1");
  test_names.push_back("Test2");
  ASSERT_TRUE(platformDelegate->CreateResultsFile(&output_path));
  ASSERT_TRUE(platformDelegate->CreateTemporaryFile(&flag_path));
  CommandLine cmd_line(platformDelegate->GetCommandLineForChildGTestProcess(
      test_names, output_path, flag_path));

  const int size = 2048;
  std::string content;
  ASSERT_TRUE(ReadFileToStringWithMaxSize(flag_path, &content, size));
  EXPECT_EQ(content.find("--gtest_filter="), 0u);
  base::ReplaceSubstringsAfterOffset(&content, 0, "--gtest_filter=", "");
  std::vector<std::string> gtest_filter_tests =
      SplitString(content, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  ASSERT_EQ(gtest_filter_tests.size(), test_names.size());
  for (unsigned i = 0; i < test_names.size(); i++) {
    EXPECT_EQ(gtest_filter_tests.at(i), test_names.at(i));
  }
}

// Mock TestLauncher to validate LaunchChildGTestProcess
// is called correctly inside the test launcher delegate.
class MockTestLauncher : public TestLauncher {
 public:
  MockTestLauncher(TestLauncherDelegate* launcher_delegate,
                   size_t parallel_jobs)
      : TestLauncher(launcher_delegate, parallel_jobs) {}

  MOCK_METHOD5(LaunchChildGTestProcess,
               void(const CommandLine& command_line,
                    const std::string& wrapper,
                    TimeDelta timeout,
                    const LaunchOptions& options,
                    std::unique_ptr<ProcessLifetimeObserver> observer));
};

// Unit tests to validate UnitTestLauncherDelegateTester implementations.
class UnitTestLauncherDelegateTester : public testing::Test {
 protected:
  TestLauncherDelegate* launcherDelegate;
  MockTestLauncher* launcher;
  std::vector<std::string> tests;

  void SetUp() override { tests.assign(100, "Test"); }

  // Setup test launcher delegate with a particular batch size.
  void SetUpLauncherDelegate(size_t batch_size) {
    launcherDelegate =
        new UnitTestLauncherDelegate(&defaultPlatform, batch_size, true);
    launcher = new MockTestLauncher(launcherDelegate, batch_size);
  }

  // Validate LaunchChildGTestProcess is called x number of times.
  void ValidateChildGTestProcessCalls(int times_called) {
    using ::testing::_;
    EXPECT_CALL(*launcher, LaunchChildGTestProcess(_, _, _, _, _))
        .Times(times_called);
  }

  void TearDown() override {
    delete launcherDelegate;
    delete launcher;
  }

 private:
  DefaultUnitTestPlatformDelegate defaultPlatform;
};

// Validate 0 batch size corresponds to unlimited batch size.
TEST_F(UnitTestLauncherDelegateTester, RunTestsWithUnlimitedBatchSize) {
  SetUpLauncherDelegate(0);

  ValidateChildGTestProcessCalls(1);
  EXPECT_EQ(launcherDelegate->RunTests(launcher, tests), tests.size());
}

// Validate edge case, no tests to run.
TEST_F(UnitTestLauncherDelegateTester, RunTestsWithEmptyTests) {
  SetUpLauncherDelegate(0);

  ValidateChildGTestProcessCalls(0);
  tests.clear();
  EXPECT_EQ(launcherDelegate->RunTests(launcher, tests), tests.size());
}

// Validate delegate slices batch size correctly.
TEST_F(UnitTestLauncherDelegateTester, RunTestsBatchSize10) {
  SetUpLauncherDelegate(10);

  ValidateChildGTestProcessCalls(10);
  EXPECT_EQ(launcherDelegate->RunTests(launcher, tests), tests.size());
}

// ValidateRetryTests will only kick-off one run.
TEST_F(UnitTestLauncherDelegateTester, RetryTests) {
  // ScopedTaskEviorment is needed since RetryTests uses thread task
  // runner to start.
  test::ScopedTaskEnvironment task_environment;
  SetUpLauncherDelegate(10);

  ValidateChildGTestProcessCalls(1);
  EXPECT_EQ(launcherDelegate->RetryTests(launcher, tests), tests.size());
  RunLoop().RunUntilIdle();
}

}  // namespace

}  // namespace base
