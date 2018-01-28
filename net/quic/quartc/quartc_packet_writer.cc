// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quartc/quartc_packet_writer.h"

namespace net {

QuartcPacketWriter::QuartcPacketWriter(
    QuartcSessionInterface::PacketTransport* packet_transport,
    QuicByteCount max_packet_size)
    : packet_transport_(packet_transport), max_packet_size_(max_packet_size) {}

WriteResult QuartcPacketWriter::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* options) {
  DCHECK(packet_transport_);
  int bytes_written = packet_transport_->Write(buffer, buf_len);
  if (bytes_written <= 0) {
    return WriteResult(WRITE_STATUS_BLOCKED, EWOULDBLOCK);
  }
  return WriteResult(WRITE_STATUS_OK, bytes_written);
}

bool QuartcPacketWriter::IsWriteBlockedDataBuffered() const {
  return false;
}

bool QuartcPacketWriter::IsWriteBlocked() const {
  DCHECK(packet_transport_);
  return !packet_transport_->CanWrite();
}

QuicByteCount QuartcPacketWriter::GetMaxPacketSize(
    const QuicSocketAddress& peer_address) const {
  return max_packet_size_;
}

void QuartcPacketWriter::SetWritable() {}

}  // namespace net
