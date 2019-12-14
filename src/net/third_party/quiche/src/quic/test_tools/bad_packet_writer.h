// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_BAD_PACKET_WRITER_H_
#define QUICHE_QUIC_TEST_TOOLS_BAD_PACKET_WRITER_H_

#include "net/third_party/quiche/src/quic/core/quic_packet_writer_wrapper.h"

namespace quic {

namespace test {
// This packet writer allows causing packet write error with specified error
// code when writing a particular packet.
class BadPacketWriter : public QuicPacketWriterWrapper {
 public:
  BadPacketWriter(size_t packet_causing_write_error, int error_code);

  ~BadPacketWriter() override;

  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override;

 private:
  size_t packet_causing_write_error_;
  int error_code_;
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_BAD_PACKET_WRITER_H_
