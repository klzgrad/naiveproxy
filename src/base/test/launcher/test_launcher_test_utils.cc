// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/launcher/test_launcher_test_utils.h"

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/optional.h"
#include "base/test/launcher/test_result.h"

namespace base {

namespace test_launcher_utils {

namespace {

// Helper function to return |Value::FindStringKey| by value instead of
// pointer to string, or empty string if nullptr.
std::string FindStringKeyOrEmpty(const Value& dict_value,
                                 const std::string& key) {
  const std::string* value = dict_value.FindStringKey(key);
  return value ? *value : std::string();
}

}  // namespace

bool ValidateKeyValue(const Value& dict_value,
                      const std::string& key,
                      const std::string& expected_value) {
  std::string actual_value = FindStringKeyOrEmpty(dict_value, key);
  bool result = !actual_value.compare(expected_value);
  if (!result)
    ADD_FAILURE() << key << " expected value: " << expected_value
                  << ", actual: " << actual_value;
  return result;
}

bool ValidateKeyValue(const Value& dict_value,
                      const std::string& key,
                      int64_t expected_value) {
  int actual_value = dict_value.FindIntKey(key).value_or(0);
  bool result = (actual_value == expected_value);
  if (!result)
    ADD_FAILURE() << key << " expected value: " << expected_value
                  << ", actual: " << actual_value;
  return result;
}

bool ValidateTestResult(const Value* iteration_data,
                        const std::string& test_name,
                        const std::string& status,
                        size_t result_part_count) {
  const Value* results = iteration_data->FindListKey(test_name);
  if (!results) {
    ADD_FAILURE() << "Cannot find result";
    return false;
  }
  if (1u != results->GetList().size()) {
    ADD_FAILURE() << "Expected one result";
    return false;
  }

  const Value& val = results->GetList().at(0);
  if (!val.is_dict()) {
    ADD_FAILURE() << "Value must be of type DICTIONARY";
    return false;
  }

  if (!ValidateKeyValue(val, "status", status))
    return false;

  const Value* value = val.FindListKey("result_parts");
  if (!value) {
    ADD_FAILURE() << "Result must contain 'result_parts' key";
    return false;
  }

  if (result_part_count != value->GetList().size()) {
    ADD_FAILURE() << "result_parts count expected: " << result_part_count
                  << ", actual:" << value->GetList().size();
    return false;
  }
  return true;
}

bool ValidateTestLocation(const Value* test_locations,
                          const std::string& test_name,
                          const std::string& file,
                          int line) {
  const Value* val = test_locations->FindDictKey(test_name);
  if (!val) {
    ADD_FAILURE() << "|test_locations| missing location for " << test_name;
    return false;
  }

  bool result = ValidateKeyValue(*val, "file", file);
  result &= ValidateKeyValue(*val, "line", line);
  return result;
}

Optional<Value> ReadSummary(const FilePath& path) {
  Optional<Value> result;
  File resultFile(path, File::FLAG_OPEN | File::FLAG_READ);
  const int size = 2e7;
  std::string json;
  CHECK(ReadFileToStringWithMaxSize(path, &json, size));
  result = JSONReader::Read(json);
  return result;
}

}  // namespace test_launcher_utils

}  // namespace base