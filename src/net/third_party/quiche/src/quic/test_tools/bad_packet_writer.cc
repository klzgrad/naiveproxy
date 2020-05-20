// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/bad_packet_writer.h"

namespace quic {
namespace test {

BadPacketWriter::BadPacketWriter(size_t packet_causing_write_error,
                                 int error_code)
    : packet_causing_write_error_(packet_causing_write_error),
      error_code_(error_code) {}

BadPacketWriter::~BadPacketWriter() {}

WriteResult BadPacketWriter::WritePacket(const char* buffer,
                                         size_t buf_len,
                                         const QuicIpAddress& self_address,
                                         const QuicSocketAddress& peer_address,
                                         PerPacketOptions* options) {
  if (error_code_ == 0 || packet_causing_write_error_ > 0) {
    if (packet_causing_write_error_ > 0) {
      --packet_causing_write_error_;
    }
    return QuicPacketWriterWrapper::WritePacket(buffer, buf_len, self_address,
                                                peer_address, options);
  }
  // It's time to cause write error.
  int error_code = error_code_;
  error_code_ = 0;
  return WriteResult(WRITE_STATUS_ERROR, error_code);
}

}  // namespace test
}  // namespace quic
