// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/test/launcher/test_result.h"
#include "base/test/launcher/test_results_tracker.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace {

// Unit tests to validate TestResultsTracker outputs the correct JSON file
// given the correct setup.
class TestResultsTrackerTester : public testing::Test {
 protected:
  void ValidateKeyValue(Value* dict_value,
                        const std::string& key,
                        int64_t expected_value) {
    base::Optional<int> value = dict_value->FindIntKey(key);
    ASSERT_TRUE(value);
    EXPECT_EQ(expected_value, value.value());
  }

  void ValidateKeyValue(Value* dict_value,
                        const std::string& key,
                        int expected_value) {
    base::Optional<int> value = dict_value->FindIntKey(key);
    ASSERT_TRUE(value);
    EXPECT_EQ(expected_value, value.value());
  }

  void ValidateKeyValue(Value* dict_value,
                        const std::string& key,
                        bool expected_value) {
    base::Optional<bool> value = dict_value->FindBoolKey(key);
    ASSERT_TRUE(value);
    EXPECT_EQ(expected_value, value.value());
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

    ValidateKeyValue(val, "elapsed_time_ms",
                     result.elapsed_time.InMilliseconds());
    ValidateKeyValue(val, "losless_snippet", true);
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
      ValidateKeyValue(part_dict, "line", result_part.line_number);
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
    ValidateKeyValue(val, "line", line);
  }

  TestResult GenerateTestResult(const std::string& test_name,
                                TestResult::Status status,
                                TimeDelta elapsed_td,
                                const std::string& output_snippet) {
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

  Optional<Value> SaveAndReadSummary() {
    ScopedTempDir dir;
    CHECK(dir.CreateUniqueTempDir());
    FilePath path = dir.GetPath().AppendASCII("SaveSummaryResult.json");
    CHECK(tracker.SaveSummaryAsJSON(path, std::vector<std::string>()));
    File resultFile(path, File::FLAG_OPEN | File::FLAG_READ);
    const int size = 2048;
    std::string json;
    CHECK(ReadFileToStringWithMaxSize(path, &json, size));
    return JSONReader::Read(json);
  }

  TestResultsTracker tracker;
};

// Validate JSON result file is saved with the correct structure.
TEST_F(TestResultsTrackerTester, JsonSummaryEmptyResult) {
  tracker.OnTestIterationStarting();

  Optional<Value> root = SaveAndReadSummary();

  ASSERT_TRUE(root);
  ASSERT_TRUE(root->is_dict());
  EXPECT_EQ(5u, root->DictSize());
}

// Validate global tags are saved correctly.
TEST_F(TestResultsTrackerTester, JsonSummaryRootTags) {
  tracker.OnTestIterationStarting();
  tracker.AddTest("Test2");  // Test should appear in alphabetical order.
  tracker.AddTest("Test1");
  tracker.AddTest("Test1");  // Test should only appear once.
  // DISABLED_ prefix should be removed.
  tracker.AddTest("DISABLED_Test3");
  tracker.AddDisabledTest("DISABLED_Test3");
  tracker.AddGlobalTag("global1");

  Optional<Value> root = SaveAndReadSummary();

  ValidateStringList(root, "global_tags", {"global1"});
  ValidateStringList(root, "all_tests", {"Test1", "Test2", "Test3"});
  ValidateStringList(root, "disabled_tests", {"Test3"});
}

// Validate test locations are saved correctly.
TEST_F(TestResultsTrackerTester, JsonSummaryTestLocation) {
  tracker.OnTestIterationStarting();
  tracker.AddTestLocation("Test1", "Test1File", 100);
  tracker.AddTestLocation("Test2", "Test2File", 200);

  Optional<Value> root = SaveAndReadSummary();

  Value* val = root->FindDictKey("test_locations");

  ASSERT_TRUE(val);
  ASSERT_TRUE(val->is_dict());
  EXPECT_EQ(2u, val->DictSize());

  ValidateTestLocation(val, "Test1", "Test1File", 100);
  ValidateTestLocation(val, "Test2", "Test2File", 200);
}

// Validate test results are saved correctly.
TEST_F(TestResultsTrackerTester, JsonSummaryTestResults) {
  TestResult test_result =
      GenerateTestResult("Test", TestResult::TEST_SUCCESS,
                         TimeDelta::FromMilliseconds(30), "output");
  test_result.test_result_parts.push_back(GenerateTestResultPart(
      TestResultPart::kSuccess, "TestFile", 110, "summary", "message"));
  tracker.AddTestPlaceholder("Test");

  tracker.OnTestIterationStarting();
  tracker.GeneratePlaceholderIteration();
  tracker.AddTestResult(test_result);

  Optional<Value> root = SaveAndReadSummary();

  Value* val = root->FindListKey("per_iteration_data");
  ASSERT_TRUE(val);
  ASSERT_EQ(1u, val->GetList().size());

  Value* iteration_val = &(val->GetList().at(0));
  ASSERT_TRUE(iteration_val);
  ASSERT_TRUE(iteration_val->is_dict());
  EXPECT_EQ(1u, iteration_val->DictSize());
  ValidateTestResult(iteration_val, test_result);
}

// Validate test results without a placeholder.
TEST_F(TestResultsTrackerTester, JsonSummaryTestWithoutPlaceholder) {
  TestResult test_result =
      GenerateTestResult("Test", TestResult::TEST_SUCCESS,
                         TimeDelta::FromMilliseconds(30), "output");

  tracker.OnTestIterationStarting();
  tracker.GeneratePlaceholderIteration();
  tracker.AddTestResult(test_result);

  Optional<Value> root = SaveAndReadSummary();

  Value* val = root->FindListKey("per_iteration_data");
  ASSERT_TRUE(val);
  ASSERT_EQ(1u, val->GetList().size());

  Value* iteration_val = &(val->GetList().at(0));
  // No result is saved since a placeholder was not specified.
  EXPECT_EQ(0u, iteration_val->DictSize());
}

// Validate test results are saved correctly based on setup.
TEST_F(TestResultsTrackerTester, JsonSummaryTestPlaceholderOrder) {
  TestResult test_result =
      GenerateTestResult("Test", TestResult::TEST_SUCCESS,
                         TimeDelta::FromMilliseconds(30), "output");
  TestResult test_result_empty = GenerateTestResult(
      "Test", TestResult::TEST_NOT_RUN, TimeDelta::FromMilliseconds(0), "");

  tracker.AddTestPlaceholder("Test");
  // Test added before GeneratePlaceholderIteration, will not record.
  tracker.OnTestIterationStarting();
  tracker.AddTestResult(test_result);
  tracker.GeneratePlaceholderIteration();
  // This is the correct order of operations.
  tracker.OnTestIterationStarting();
  tracker.GeneratePlaceholderIteration();
  tracker.AddTestResult(test_result);

  Optional<Value> root = SaveAndReadSummary();
  Value* val = root->FindListKey("per_iteration_data");
  ASSERT_TRUE(val);
  ASSERT_EQ(2u, val->GetList().size());

  Value* iteration_val = &(val->GetList().at(0));
  ValidateTestResult(iteration_val, test_result_empty);

  iteration_val = &(val->GetList().at(1));
  ValidateTestResult(iteration_val, test_result);
}

// Disabled tests results are not saved in json summary.
TEST_F(TestResultsTrackerTester, JsonSummaryDisabledTestResults) {
  tracker.AddDisabledTest("Test");
  TestResult test_result =
      GenerateTestResult("Test", TestResult::TEST_SUCCESS,
                         TimeDelta::FromMilliseconds(30), "output");
  test_result.test_result_parts.push_back(GenerateTestResultPart(
      TestResultPart::kSuccess, "TestFile", 110, "summary", "message"));
  tracker.AddTestPlaceholder("Test");

  tracker.OnTestIterationStarting();
  tracker.GeneratePlaceholderIteration();
  tracker.AddTestResult(test_result);

  Optional<Value> root = SaveAndReadSummary();

  Value* val = root->FindListKey("per_iteration_data");
  ASSERT_TRUE(val);
  ASSERT_EQ(1u, val->GetList().size());

  Value* iteration_val = &(val->GetList().at(0));
  // No result is saved since a placeholder was not specified.
  EXPECT_EQ(0u, iteration_val->DictSize());
}

}  // namespace

}  // namespace base
