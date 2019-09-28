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
// Mock TestLauncher to validate LaunchChildGTestProcess
// is called correctly inside the test launcher delegate.
class MockTestLauncher : public TestLauncher {
 public:
  MockTestLauncher(TestLauncherDelegate* launcher_delegate,
                   size_t parallel_jobs)
      : TestLauncher(launcher_delegate, parallel_jobs) {}

  MOCK_METHOD3(LaunchChildGTestProcess,
               void(scoped_refptr<TaskRunner> task_runner,
                    const std::vector<std::string>& test_names,
                    const FilePath& temp_dir));
};

// Unit tests to validate UnitTestLauncherDelegateTester implementations.
class UnitTestLauncherDelegateTester : public testing::Test {
 protected:
  DefaultUnitTestPlatformDelegate defaultPlatform;
};

// Validate delegate produces correct command line.
TEST_F(UnitTestLauncherDelegateTester, GetCommandLine) {
  UnitTestLauncherDelegate launcher_delegate(&defaultPlatform, 10u, true);
  TestLauncherDelegate* delegate_ptr = &launcher_delegate;

  std::vector<std::string> test_names(5, "Tests");
  base::FilePath temp_dir;
  base::FilePath result_file;
  CreateNewTempDirectory(FilePath::StringType(), &temp_dir);

  CommandLine cmd_line =
      delegate_ptr->GetCommandLine(test_names, temp_dir, &result_file);
  EXPECT_TRUE(cmd_line.HasSwitch("single-process-tests"));
  EXPECT_EQ(cmd_line.GetSwitchValuePath("test-launcher-output"), result_file);

  const int size = 2048;
  std::string content;
  ASSERT_TRUE(ReadFileToStringWithMaxSize(
      cmd_line.GetSwitchValuePath("gtest_flagfile"), &content, size));
  EXPECT_EQ(content.find("--gtest_filter="), 0u);
  base::ReplaceSubstringsAfterOffset(&content, 0, "--gtest_filter=", "");
  std::vector<std::string> gtest_filter_tests =
      SplitString(content, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  ASSERT_EQ(gtest_filter_tests.size(), test_names.size());
  for (unsigned i = 0; i < test_names.size(); i++) {
    EXPECT_EQ(gtest_filter_tests.at(i), test_names.at(i));
  }
}

// Validate delegate sets batch size correctly.
TEST_F(UnitTestLauncherDelegateTester, BatchSize) {
  UnitTestLauncherDelegate launcher_delegate(&defaultPlatform, 15u, true);
  TestLauncherDelegate* delegate_ptr = &launcher_delegate;
  EXPECT_EQ(delegate_ptr->GetBatchSize(), 15u);
}

}  // namespace

}  // namespace base
