// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_TEST_OUTPUT_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_TEST_OUTPUT_H_

#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quic/platform/impl/quic_test_output_impl.h"

namespace quic {

// Records a QUIC test output file into a directory specified by QUIC_TRACE_DIR
// environment variable.  Assumes that it's called from a unit test.
//
// The |identifier| is a human-readable identifier that will be combined with
// the name of the unit test and a timestamp.  |data| is the test output data
// that is being recorded into the file.
inline void QuicRecordTestOutput(QuicStringPiece identifier,
                                 QuicStringPiece data) {
  QuicRecordTestOutputImpl(identifier, data);
}

}  // namespace quic
#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_TEST_OUTPUT_H_
