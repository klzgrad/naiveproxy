// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/send_algorithm_test_utils.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_output.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {
namespace test {

bool LoadSendAlgorithmTestResult(SendAlgorithmTestResult* result) {
  std::string test_result_file_content;
  if (!QuicLoadTestOutput(GetSendAlgorithmTestResultFilename(),
                          &test_result_file_content)) {
    return false;
  }
  return result->ParseFromString(test_result_file_content);
}

void RecordSendAlgorithmTestResult(uint64_t random_seed,
                                   int64_t simulated_duration_micros) {
  SendAlgorithmTestResult result;
  result.set_test_name(GetFullSendAlgorithmTestName());
  result.set_random_seed(random_seed);
  result.set_simulated_duration_micros(simulated_duration_micros);

  QuicSaveTestOutput(GetSendAlgorithmTestResultFilename(),
                     result.SerializeAsString());
}

void CompareSendAlgorithmTestResult(int64_t actual_simulated_duration_micros) {
  SendAlgorithmTestResult expected;
  ASSERT_TRUE(LoadSendAlgorithmTestResult(&expected));
  QUIC_LOG(INFO) << "Loaded expected test result: "
                 << expected.ShortDebugString();

  EXPECT_GE(expected.simulated_duration_micros(),
            actual_simulated_duration_micros);
}

std::string GetFullSendAlgorithmTestName() {
  const auto* test_info =
      ::testing::UnitTest::GetInstance()->current_test_info();
  const std::string type_param =
      test_info->type_param() ? test_info->type_param() : "";
  const std::string value_param =
      test_info->value_param() ? test_info->value_param() : "";
  return quiche::QuicheStrCat(test_info->test_suite_name(), ".",
                              test_info->name(), "_", type_param, "_",
                              value_param);
}

std::string GetSendAlgorithmTestResultFilename() {
  return GetFullSendAlgorithmTestName() + ".test_result";
}

}  // namespace test
}  // namespace quic
