// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_OUTPUT_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_OUTPUT_H_

#include "quiche_platform_impl/quiche_test_output_impl.h"
#include "absl/strings/string_view.h"

namespace quiche {

// Save |data| into ${QUICHE_TEST_OUTPUT_DIR}/filename. If a file with the same
// path already exists, overwrite it.
inline void QuicheSaveTestOutput(absl::string_view filename,
                                 absl::string_view data) {
  QuicheSaveTestOutputImpl(filename, data);
}

// Load the content of ${QUICHE_TEST_OUTPUT_DIR}/filename into |*data|.
// Return whether it is successfully loaded.
inline bool QuicheLoadTestOutput(absl::string_view filename,
                                 std::string* data) {
  return QuicheLoadTestOutputImpl(filename, data);
}

// Records a QUIC trace file(.qtr) into a directory specified by the
// QUICHE_TEST_OUTPUT_DIR environment variable.  Assumes that it's called from a
// unit test.
//
// The |identifier| is a human-readable identifier that will be combined with
// the name of the unit test and a timestamp.  |data| is the serialized
// quic_trace.Trace protobuf that is being recorded into the file.
inline void QuicheRecordTrace(absl::string_view identifier,
                              absl::string_view data) {
  QuicheRecordTraceImpl(identifier, data);
}

}  // namespace quiche
#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_OUTPUT_H_
