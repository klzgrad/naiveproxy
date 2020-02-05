// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_header_table_peer.h"

#include "net/third_party/quiche/src/quic/core/qpack/qpack_header_table.h"

namespace quic {
namespace test {

// static
uint64_t QpackHeaderTablePeer::dynamic_table_capacity(
    const QpackHeaderTable* header_table) {
  return header_table->dynamic_table_capacity_;
}

// static
uint64_t QpackHeaderTablePeer::maximum_dynamic_table_capacity(
    const QpackHeaderTable* header_table) {
  return header_table->maximum_dynamic_table_capacity_;
}

}  // namespace test
}  // namespace quic
