// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_HEADER_TABLE_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_HEADER_TABLE_PEER_H_

#include <cstdint>

namespace quic {

class QpackHeaderTable;

namespace test {

class QpackHeaderTablePeer {
 public:
  QpackHeaderTablePeer() = delete;

  static uint64_t dynamic_table_capacity(const QpackHeaderTable* header_table);
  static uint64_t maximum_dynamic_table_capacity(
      const QpackHeaderTable* header_table);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_HEADER_TABLE_PEER_H_
