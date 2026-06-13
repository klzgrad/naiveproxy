// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_TEST_OUTPUT_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_TEST_OUTPUT_H_

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_test_output.h"

namespace quic {

inline void QuicSaveTestOutput(absl::string_view filename,
                               absl::string_view data) {
  quiche::QuicheSaveTestOutput(filename, data);
}

inline bool QuicLoadTestOutput(absl::string_view filename, std::string* data) {
  return quiche::QuicheLoadTestOutput(filename, data);
}

inline void QuicRecordTrace(absl::string_view identifier,
                            absl::string_view data) {
  quiche::QuicheRecordTrace(identifier, data);
}

}  // namespace quic
#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_TEST_OUTPUT_H_
