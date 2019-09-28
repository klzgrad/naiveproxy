// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/base64.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/test/launcher/test_launcher.h"
#include "base/test/launcher/unit_test_launcher.h"
#include "base/test/scoped_task_environment.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread_task_runner_handle.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace {

TestResult GenerateTestResult(
    const std::string& test_name,
    TestResult::Status status,
    TimeDelta elapsed_td = TimeDelta::FromMilliseconds(30),
    const std::string& output_snippet = "output") {
  TestResult result;
  result.full_name = test_name;
  result.status = status;
  result.elapsed_time = elapsed_td;
  result.output_snippet = output_snippet;
  return result;
}

TestResultPart GenerateTestResultPart(TestResultPart::Type type,
                                      const std::string& file_name,
                                      int line_number,
                                      const std::string& summary,
                                      const std::string& message) {
  TestResultPart test_result_part;
  test_result_part.type = type;
  test_result_part.file_name = file_name;
  test_result_part.line_number = line_number;
  test_result_part.summary = summary;
  test_result_part.message = message;
  return test_result_part;
}

// Mock TestLauncher to mock CreateAndStartThreadPool,
// unit test will provide a ScopedTaskEnvironment.
class MockTestLauncher : public TestLauncher {
 public:
  MockTestLauncher(TestLauncherDelegate* launcher_delegate,
                   size_t parallel_jobs)
      : TestLauncher(launcher_delegate, parallel_jobs) {}

  void CreateAndStartThreadPool(int parallel_jobs) override {}

  MOCK_METHOD3(LaunchChildGTestProcess,
               void(scoped_refptr<TaskRunner> task_runner,
                    const std::vector<std::string>& test_names,
                    const FilePath& temp_dir));
};

// Simple TestLauncherDelegate mock to test TestLauncher flow.
class MockTestLauncherDelegate : public TestLauncherDelegate {
 public:
  MOCK_METHOD1(GetTests, bool(std::vector<TestIdentifier>* output));
  MOCK_METHOD2(WillRunTest,
               bool(const std::string& test_case_name,
                    const std::string& test_name));
  MOCK_METHOD6(
      ProcessTestResults,
      std::vector<TestResult>(const std::vector<std::string>& test_names,
                              const base::FilePath& output_file,
                              const std::string& output,
                              const base::TimeDelta& elapsed_time,
                              int exit_code,
                              bool was_timeout));
  MOCK_METHOD3(GetCommandLine,
               CommandLine(const std::vector<std::string>& test_names,
                           const FilePath& temp_dir_,
                           FilePath* output_file_));
  MOCK_METHOD1(IsPreTask, bool(const std::vector<std::string>& test_names));
  MOCK_METHOD0(GetWrapper, std::string());
  MOCK_METHOD0(GetLaunchOptions, int());
  MOCK_METHOD0(GetTimeout, TimeDelta());
  MOCK_METHOD0(GetBatchSize, size_t());
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
  void SetUpExpectCalls(size_t batch_size = 10) {
    using ::testing::_;
    EXPECT_CALL(delegate, GetTests(_))
        .WillOnce(::testing::DoAll(testing::SetArgPointee<0>(tests_),
                                   testing::Return(true)));
    EXPECT_CALL(delegate, WillRunTest(_, _))
        .WillRepeatedly(testing::Return(true));
    EXPECT_CALL(delegate, ProcessTestResults(_, _, _, _, _, _)).Times(0);
    EXPECT_CALL(delegate, GetCommandLine(_, _, _))
        .WillRepeatedly(testing::Return(CommandLine(CommandLine::NO_PROGRAM)));
    EXPECT_CALL(delegate, GetWrapper())
        .WillRepeatedly(testing::Return(std::string()));
    EXPECT_CALL(delegate, IsPreTask(_)).WillRepeatedly(testing::Return(true));
    EXPECT_CALL(delegate, GetLaunchOptions())
        .WillRepeatedly(testing::Return(true));
    EXPECT_CALL(delegate, GetTimeout())
        .WillRepeatedly(testing::Return(TimeDelta()));
    EXPECT_CALL(delegate, GetBatchSize())
        .WillRepeatedly(testing::Return(batch_size));
  }

  void ReadSummary(FilePath path) {
    File resultFile(path, File::FLAG_OPEN | File::FLAG_READ);
    const int size = 2048;
    std::string json;
    ASSERT_TRUE(ReadFileToStringWithMaxSize(path, &json, size));
    root = JSONReader::Read(json);
  }

  std::unique_ptr<CommandLine> command_line;
  MockTestLauncher test_launcher;
  MockTestLauncherDelegate delegate;
  base::test::ScopedTaskEnvironment scoped_task_environment;
  ScopedTempDir dir;
  Optional<Value> root;

 private:
  std::vector<TestIdentifier> tests_;
};

// Action to mock delegate invoking OnTestFinish on test launcher.
ACTION_P3(OnTestResult, launcher, full_name, status) {
  TestResult result = GenerateTestResult(full_name, status);
  arg0->PostTask(FROM_HERE, BindOnce(&TestLauncher::OnTestFinished,
                                     Unretained(launcher), result));
}

// Action to mock delegate invoking OnTestFinish on test launcher.
ACTION_P2(OnTestResult, launcher, result) {
  arg0->PostTask(FROM_HERE, BindOnce(&TestLauncher::OnTestFinished,
                                     Unretained(launcher), result));
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

// When There are no tests, delegate should not be called.
TEST_F(TestLauncherTest, EmptyTestSetPasses) {
  SetUpExpectCalls();
  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(_, _, _)).Times(0);
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher filters DISABLED tests by default.
TEST_F(TestLauncherTest, FilterDisabledTestByDefault) {
  AddMockedTests("DISABLED_TestDisabled", {"firstTest"});
  AddMockedTests("Test",
                 {"firstTest", "secondTest", "DISABLED_firstTestDisabled"});
  SetUpExpectCalls();
  std::vector<std::string> tests_names = {"Test.firstTest", "Test.secondTest"};
  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .WillOnce(::testing::DoAll(OnTestResult(&test_launcher, "Test.firstTest",
                                              TestResult::TEST_SUCCESS),
                                 OnTestResult(&test_launcher, "Test.secondTest",
                                              TestResult::TEST_SUCCESS)));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher should reorder PRE_ tests before delegate
TEST_F(TestLauncherTest, ReorderPreTests) {
  AddMockedTests("Test", {"firstTest", "PRE_PRE_firstTest", "PRE_firstTest"});
  SetUpExpectCalls();
  std::vector<std::string> tests_names = {
      "Test.PRE_PRE_firstTest", "Test.PRE_firstTest", "Test.firstTest"};
  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .Times(1);
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
  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .WillOnce(OnTestResult(&test_launcher, "Test.firstTest",
                             TestResult::TEST_SUCCESS));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher gtest filter will include pre tests
TEST_F(TestLauncherTest, FilterIncludePreTest) {
  AddMockedTests("Test", {"firstTest", "secondTest", "PRE_firstTest"});
  SetUpExpectCalls();
  command_line->AppendSwitchASCII("gtest_filter", "Test.firstTest");
  std::vector<std::string> tests_names = {"Test.PRE_firstTest",
                                          "Test.firstTest"};
  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .Times(1);
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher "gtest_repeat" switch.
TEST_F(TestLauncherTest, RunningMultipleIterations) {
  AddMockedTests("Test", {"firstTest"});
  SetUpExpectCalls();
  command_line->AppendSwitchASCII("gtest_repeat", "2");
  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(_, _, _))
      .Times(2)
      .WillRepeatedly(OnTestResult(&test_launcher, "Test.firstTest",
                                   TestResult::TEST_SUCCESS));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher will retry failed test, and stop on success.
TEST_F(TestLauncherTest, SuccessOnRetryTests) {
  AddMockedTests("Test", {"firstTest"});
  SetUpExpectCalls();
  using ::testing::_;
  std::vector<std::string> tests_names = {"Test.firstTest"};
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .WillOnce(OnTestResult(&test_launcher, "Test.firstTest",
                             TestResult::TEST_FAILURE))
      .WillOnce(OnTestResult(&test_launcher, "Test.firstTest",
                             TestResult::TEST_SUCCESS));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher will retry continuing failing test 3 times by default,
// before eventually failing and returning false.
TEST_F(TestLauncherTest, FailOnRetryTests) {
  AddMockedTests("Test", {"firstTest"});
  SetUpExpectCalls();
  using ::testing::_;
  std::vector<std::string> tests_names = {"Test.firstTest"};
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .Times(4)
      .WillRepeatedly(OnTestResult(&test_launcher, "Test.firstTest",
                                   TestResult::TEST_FAILURE));
  EXPECT_FALSE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher should retry all PRE_ chained tests
TEST_F(TestLauncherTest, RetryPreTests) {
  AddMockedTests("Test", {"firstTest", "PRE_PRE_firstTest", "PRE_firstTest"});
  SetUpExpectCalls();
  std::vector<TestResult> results = {
      GenerateTestResult("Test.PRE_PRE_firstTest", TestResult::TEST_SUCCESS),
      GenerateTestResult("Test.PRE_firstTest", TestResult::TEST_FAILURE),
      GenerateTestResult("Test.firstTest", TestResult::TEST_SUCCESS)};
  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(_, _, _))
      .WillOnce(::testing::DoAll(
          OnTestResult(&test_launcher, "Test.PRE_PRE_firstTest",
                       TestResult::TEST_SUCCESS),
          OnTestResult(&test_launcher, "Test.PRE_firstTest",
                       TestResult::TEST_FAILURE),
          OnTestResult(&test_launcher, "Test.firstTest",
                       TestResult::TEST_SUCCESS)));
  std::vector<std::string> tests_names = {"Test.PRE_PRE_firstTest"};
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .WillOnce(OnTestResult(&test_launcher, "Test.PRE_PRE_firstTest",
                             TestResult::TEST_SUCCESS));
  tests_names = {"Test.PRE_firstTest"};
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .WillOnce(OnTestResult(&test_launcher, "Test.PRE_firstTest",
                             TestResult::TEST_SUCCESS));
  tests_names = {"Test.firstTest"};
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .WillOnce(OnTestResult(&test_launcher, "Test.firstTest",
                             TestResult::TEST_SUCCESS));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Test TestLauncher run disabled unit tests switch.
TEST_F(TestLauncherTest, RunDisabledTests) {
  AddMockedTests("DISABLED_TestDisabled", {"firstTest"});
  AddMockedTests("Test",
                 {"firstTest", "secondTest", "DISABLED_firstTestDisabled"});
  SetUpExpectCalls();
  command_line->AppendSwitch("gtest_also_run_disabled_tests");
  command_line->AppendSwitchASCII("gtest_filter", "Test*.first*");
  std::vector<std::string> tests_names = {"DISABLED_TestDisabled.firstTest",
                                          "Test.firstTest",
                                          "Test.DISABLED_firstTestDisabled"};
  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .WillOnce(::testing::DoAll(
          OnTestResult(&test_launcher, "Test.firstTest",
                       TestResult::TEST_SUCCESS),
          OnTestResult(&test_launcher, "DISABLED_TestDisabled.firstTest",
                       TestResult::TEST_SUCCESS),
          OnTestResult(&test_launcher, "Test.DISABLED_firstTestDisabled",
                       TestResult::TEST_SUCCESS)));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Disabled test should disable all pre tests
TEST_F(TestLauncherTest, DisablePreTests) {
  AddMockedTests("Test", {"DISABLED_firstTest", "PRE_PRE_firstTest",
                          "PRE_firstTest", "secondTest"});
  SetUpExpectCalls();
  std::vector<std::string> tests_names = {"Test.secondTest"};
  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(
                                 _,
                                 testing::ElementsAreArray(tests_names.cbegin(),
                                                           tests_names.cend()),
                                 _))
      .Times(1);
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

// Shard index must be lesser than total shards
TEST_F(TestLauncherTest, FaultyShardSetup) {
  command_line->AppendSwitchASCII("test-launcher-total-shards", "2");
  command_line->AppendSwitchASCII("test-launcher-shard-index", "2");
  EXPECT_FALSE(test_launcher.Run(command_line.get()));
}

// Shard index must be lesser than total shards
TEST_F(TestLauncherTest, RedirectStdio) {
  AddMockedTests("Test", {"firstTest"});
  SetUpExpectCalls();
  command_line->AppendSwitchASCII("test-launcher-print-test-stdio", "always");
  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(_, _, _))
      .WillOnce(OnTestResult(&test_launcher, "Test.firstTest",
                             TestResult::TEST_SUCCESS));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));
}

void ValidateKeyValue(Value* dict_value,
                      const std::string& key,
                      const std::string& expected_value) {
  const std::string* value = dict_value->FindStringKey(key);
  ASSERT_TRUE(value);
  EXPECT_EQ(expected_value, *value);
}

// Validate a json child node for a particular test result.
void ValidateTestResult(Value* root, TestResult& result) {
  Value* val = root->FindListKey(result.full_name);
  ASSERT_TRUE(val);
  ASSERT_EQ(1u, val->GetList().size());
  val = &val->GetList().at(0);
  ASSERT_TRUE(val->is_dict());

  EXPECT_EQ(result.elapsed_time.InMilliseconds(),
            val->FindIntKey("elapsed_time_ms").value_or(0));
  EXPECT_EQ(true, val->FindBoolKey("losless_snippet").value_or(false));
  ValidateKeyValue(val, "output_snippet", result.output_snippet);

  std::string base64_output_snippet;
  Base64Encode(result.output_snippet, &base64_output_snippet);
  ValidateKeyValue(val, "output_snippet_base64", base64_output_snippet);
  ValidateKeyValue(val, "status", result.StatusAsString());

  Value* value = val->FindListKey("result_parts");
  ASSERT_TRUE(value);
  EXPECT_EQ(result.test_result_parts.size(), value->GetList().size());
  for (unsigned i = 0; i < result.test_result_parts.size(); i++) {
    TestResultPart result_part = result.test_result_parts.at(0);
    Value* part_dict = &(value->GetList().at(i));
    ASSERT_TRUE(part_dict);
    ASSERT_TRUE(part_dict->is_dict());
    ValidateKeyValue(part_dict, "type", result_part.TypeAsString());
    ValidateKeyValue(part_dict, "file", result_part.file_name);
    EXPECT_EQ(result_part.line_number,
              part_dict->FindIntKey("line").value_or(0));
    ValidateKeyValue(part_dict, "summary", result_part.summary);
    ValidateKeyValue(part_dict, "message", result_part.message);
  }
}

void ValidateStringList(Optional<Value>& root,
                        const std::string& key,
                        std::vector<const char*> values) {
  Value* val = root->FindListKey(key);
  ASSERT_TRUE(val);
  ASSERT_EQ(values.size(), val->GetList().size());
  for (unsigned i = 0; i < values.size(); i++) {
    ASSERT_TRUE(val->GetList().at(i).is_string());
    EXPECT_EQ(values.at(i), val->GetList().at(i).GetString());
  }
}

void ValidateTestLocation(Value* root,
                          const std::string& key,
                          const std::string& file,
                          int line) {
  Value* val = root->FindDictKey(key);
  ASSERT_TRUE(val);
  EXPECT_EQ(2u, val->DictSize());
  ValidateKeyValue(val, "file", file);
  EXPECT_EQ(line, val->FindIntKey("line").value_or(0));
}

// Unit tests to validate TestLauncher outputs the correct JSON file.
TEST_F(TestLauncherTest, JsonSummary) {
  AddMockedTests("DISABLED_TestDisabled", {"firstTest"});
  AddMockedTests("Test",
                 {"firstTest", "secondTest", "DISABLED_firstTestDisabled"});
  SetUpExpectCalls();

  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath path = dir.GetPath().AppendASCII("SaveSummaryResult.json");
  command_line->AppendSwitchPath("test-launcher-summary-output", path);
  command_line->AppendSwitchASCII("gtest_repeat", "2");

  // Setup results to be returned by the test launcher delegate.
  TestResult first_result =
      GenerateTestResult("Test.firstTest", TestResult::TEST_SUCCESS,
                         TimeDelta::FromMilliseconds(30), "output_first");
  first_result.test_result_parts.push_back(GenerateTestResultPart(
      TestResultPart::kSuccess, "TestFile", 110, "summary", "message"));
  TestResult second_result =
      GenerateTestResult("Test.secondTest", TestResult::TEST_SUCCESS,
                         TimeDelta::FromMilliseconds(50), "output_second");

  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(_, _, _))
      .Times(2)
      .WillRepeatedly(
          ::testing::DoAll(OnTestResult(&test_launcher, first_result),
                           OnTestResult(&test_launcher, second_result)));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));

  // Validate the resulting JSON file is the expected output.
  ReadSummary(path);
  ValidateStringList(root, "all_tests",
                     {"Test.firstTest", "Test.firstTestDisabled",
                      "Test.secondTest", "TestDisabled.firstTest"});
  ValidateStringList(root, "disabled_tests",
                     {"Test.firstTestDisabled", "TestDisabled.firstTest"});

  Value* val = root->FindDictKey("test_locations");
  ASSERT_TRUE(val);
  EXPECT_EQ(2u, val->DictSize());
  ValidateTestLocation(val, "Test.firstTest", "File", 100);
  ValidateTestLocation(val, "Test.secondTest", "File", 100);

  val = root->FindListKey("per_iteration_data");
  ASSERT_TRUE(val);
  ASSERT_EQ(2u, val->GetList().size());
  for (size_t i = 0; i < val->GetList().size(); i++) {
    Value* iteration_val = &(val->GetList().at(i));
    ASSERT_TRUE(iteration_val);
    ASSERT_TRUE(iteration_val->is_dict());
    EXPECT_EQ(2u, iteration_val->DictSize());
    ValidateTestResult(iteration_val, first_result);
    ValidateTestResult(iteration_val, second_result);
  }
}

// Validate TestLauncher outputs the correct JSON file
// when running disabled tests.
TEST_F(TestLauncherTest, JsonSummaryWithDisabledTests) {
  AddMockedTests("Test", {"DISABLED_Test"});
  SetUpExpectCalls();

  ASSERT_TRUE(dir.CreateUniqueTempDir());
  FilePath path = dir.GetPath().AppendASCII("SaveSummaryResult.json");
  command_line->AppendSwitchPath("test-launcher-summary-output", path);
  command_line->AppendSwitch("gtest_also_run_disabled_tests");

  // Setup results to be returned by the test launcher delegate.
  TestResult test_result =
      GenerateTestResult("Test.DISABLED_Test", TestResult::TEST_SUCCESS,
                         TimeDelta::FromMilliseconds(50), "output_second");

  using ::testing::_;
  EXPECT_CALL(test_launcher, LaunchChildGTestProcess(_, _, _))
      .WillOnce(OnTestResult(&test_launcher, test_result));
  EXPECT_TRUE(test_launcher.Run(command_line.get()));

  // Validate the resulting JSON file is the expected output.
  ReadSummary(path);
  Value* val = root->FindDictKey("test_locations");
  ASSERT_TRUE(val);
  EXPECT_EQ(1u, val->DictSize());
  ValidateTestLocation(val, "Test.DISABLED_Test", "File", 100);

  val = root->FindListKey("per_iteration_data");
  ASSERT_TRUE(val);
  ASSERT_EQ(1u, val->GetList().size());

  Value* iteration_val = &(val->GetList().at(0));
  ASSERT_TRUE(iteration_val);
  ASSERT_TRUE(iteration_val->is_dict());
  EXPECT_EQ(1u, iteration_val->DictSize());
  // We expect the result to be stripped of disabled prefix.
  test_result.full_name = "Test.Test";
  ValidateTestResult(iteration_val, test_result);
}

}  // namespace

}  // namespace base
