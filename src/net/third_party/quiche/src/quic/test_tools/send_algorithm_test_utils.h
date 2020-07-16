// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SEND_ALGORITHM_TEST_UTILS_H_
#define QUICHE_QUIC_TEST_TOOLS_SEND_ALGORITHM_TEST_UTILS_H_

#include "net/third_party/quiche/src/quic/test_tools/send_algorithm_test_result.pb.h"

namespace quic {
namespace test {

bool LoadSendAlgorithmTestResult(SendAlgorithmTestResult* result);

void RecordSendAlgorithmTestResult(uint64_t random_seed,
                                   int64_t simulated_duration_micros);

// Load the expected test result with LoadSendAlgorithmTestResult(), and compare
// it with the actual results provided in the arguments.
void CompareSendAlgorithmTestResult(int64_t actual_simulated_duration_micros);

std::string GetFullSendAlgorithmTestName();

std::string GetSendAlgorithmTestResultFilename();

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_SEND_ALGORITHM_TEST_UTILS_H_
