// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/test_tools/packet_reordering_writer.h"

namespace net {
namespace test {

PacketReorderingWriter::PacketReorderingWriter() {}

PacketReorderingWriter::~PacketReorderingWriter() {}

WriteResult PacketReorderingWriter::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* options) {
  if (!delay_next_) {
    WriteResult wr = QuicPacketWriterWrapper::WritePacket(
        buffer, buf_len, self_address, peer_address, options);
    --num_packets_to_wait_;
    if (num_packets_to_wait_ == 0) {
      // It's time to write the delayed packet.
      QuicPacketWriterWrapper::WritePacket(
          delayed_data_.data(), delayed_data_.length(), delayed_self_address_,
          delayed_peer_address_, delayed_options_.get());
    }
    return wr;
  }
  // Still have packet to wait.
  DCHECK_LT(0u, num_packets_to_wait_) << "Only allow one packet to be delayed";
  delayed_data_ = std::string(buffer, buf_len);
  delayed_self_address_ = self_address;
  delayed_peer_address_ = peer_address;
  if (options != nullptr) {
    delayed_options_.reset(options->Clone());
  }
  delay_next_ = false;
  return WriteResult(WRITE_STATUS_OK, buf_len);
}

void PacketReorderingWriter::SetDelay(size_t num_packets_to_wait) {
  DCHECK_GT(num_packets_to_wait, 0u);
  num_packets_to_wait_ = num_packets_to_wait;
  delay_next_ = true;
}

}  // namespace test
}  // namespace net
