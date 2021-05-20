// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_ENCODER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_ENCODER_PEER_H_

#include <cstdint>

namespace quic {

class QpackEncoder;
class QpackHeaderTable;

namespace test {

class QpackEncoderPeer {
 public:
  QpackEncoderPeer() = delete;

  static QpackHeaderTable* header_table(QpackEncoder* encoder);
  static uint64_t maximum_blocked_streams(const QpackEncoder* encoder);
  static uint64_t smallest_blocking_index(const QpackEncoder* encoder);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_ENCODER_PEER_H_
