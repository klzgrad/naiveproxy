// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_TEST_OUTPUT_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_TEST_OUTPUT_H_

#include "net/quic/platform/impl/quic_test_output_impl.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// Save |data| into ${QUIC_TEST_OUTPUT_DIR}/filename. If a file with the same
// path already exists, overwrite it.
inline void QuicSaveTestOutput(quiche::QuicheStringPiece filename,
                               quiche::QuicheStringPiece data) {
  QuicSaveTestOutputImpl(filename, data);
}

// Load the content of ${QUIC_TEST_OUTPUT_DIR}/filename into |*data|.
// Return whether it is successfully loaded.
inline bool QuicLoadTestOutput(quiche::QuicheStringPiece filename,
                               std::string* data) {
  return QuicLoadTestOutputImpl(filename, data);
}

// Records a QUIC trace file(.qtr) into a directory specified by the
// QUIC_TEST_OUTPUT_DIR environment variable.  Assumes that it's called from a
// unit test.
//
// The |identifier| is a human-readable identifier that will be combined with
// the name of the unit test and a timestamp.  |data| is the serialized
// quic_trace.Trace protobuf that is being recorded into the file.
inline void QuicRecordTrace(quiche::QuicheStringPiece identifier,
                            quiche::QuicheStringPiece data) {
  QuicRecordTraceImpl(identifier, data);
}

}  // namespace quic
#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_TEST_OUTPUT_H_
