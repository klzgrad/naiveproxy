// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/qpack/qpack_encoder_peer.h"

#include "quiche/quic/core/qpack/qpack_encoder.h"

namespace quic {
namespace test {

// static
QpackEncoderHeaderTable* QpackEncoderPeer::header_table(QpackEncoder* encoder) {
  return &encoder->header_table_;
}

// static
uint64_t QpackEncoderPeer::maximum_blocked_streams(
    const QpackEncoder* encoder) {
  return encoder->maximum_blocked_streams_;
}

// static
uint64_t QpackEncoderPeer::smallest_blocking_index(
    const QpackEncoder* encoder) {
  return encoder->blocking_manager_.smallest_blocking_index();
}

}  // namespace test
}  // namespace quic
