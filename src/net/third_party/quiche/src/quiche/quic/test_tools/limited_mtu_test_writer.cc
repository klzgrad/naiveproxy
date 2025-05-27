// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/limited_mtu_test_writer.h"

namespace quic {
namespace test {

LimitedMtuTestWriter::LimitedMtuTestWriter(QuicByteCount mtu) : mtu_(mtu) {}

LimitedMtuTestWriter::~LimitedMtuTestWriter() = default;

WriteResult LimitedMtuTestWriter::WritePacket(
    const char* buffer, size_t buf_len, const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address, PerPacketOptions* options,
    const QuicPacketWriterParams& params) {
  if (buf_len > mtu_) {
    // Drop the packet.
    return WriteResult(WRITE_STATUS_OK, buf_len);
  }

  return QuicPacketWriterWrapper::WritePacket(buffer, buf_len, self_address,
                                              peer_address, options, params);
}

}  // namespace test
}  // namespace quic
