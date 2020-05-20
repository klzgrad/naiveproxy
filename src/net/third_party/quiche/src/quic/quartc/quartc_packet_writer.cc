// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/quartc_packet_writer.h"

#include <utility>

namespace quic {

std::unique_ptr<PerPacketOptions> QuartcPerPacketOptions::Clone() const {
  return std::make_unique<QuartcPerPacketOptions>(*this);
}

QuartcPacketWriter::QuartcPacketWriter(QuartcPacketTransport* packet_transport,
                                       QuicByteCount max_packet_size)
    : packet_transport_(packet_transport), max_packet_size_(max_packet_size) {}

WriteResult QuartcPacketWriter::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& /*self_address*/,
    const QuicSocketAddress& /*peer_address*/,
    PerPacketOptions* options) {
  DCHECK(packet_transport_);

  QuartcPacketTransport::PacketInfo info;
  QuartcPerPacketOptions* quartc_options =
      static_cast<QuartcPerPacketOptions*>(options);
  if (quartc_options && quartc_options->connection) {
    info.packet_number =
        quartc_options->connection->packet_creator().packet_number();
  }
  int bytes_written = packet_transport_->Write(buffer, buf_len, info);
  if (bytes_written <= 0) {
    writable_ = false;
    return WriteResult(WRITE_STATUS_BLOCKED, EWOULDBLOCK);
  }
  return WriteResult(WRITE_STATUS_OK, bytes_written);
}

bool QuartcPacketWriter::IsWriteBlocked() const {
  return !writable_;
}

QuicByteCount QuartcPacketWriter::GetMaxPacketSize(
    const QuicSocketAddress& /*peer_address*/) const {
  return max_packet_size_;
}

void QuartcPacketWriter::SetWritable() {
  writable_ = true;
}

bool QuartcPacketWriter::SupportsReleaseTime() const {
  return false;
}

bool QuartcPacketWriter::IsBatchMode() const {
  return false;
}

char* QuartcPacketWriter::GetNextWriteLocation(
    const QuicIpAddress& /*self_address*/,
    const QuicSocketAddress& /*peer_address*/) {
  return nullptr;
}

WriteResult QuartcPacketWriter::Flush() {
  return WriteResult(WRITE_STATUS_OK, 0);
}

void QuartcPacketWriter::SetPacketTransportDelegate(
    QuartcPacketTransport::Delegate* delegate) {
  packet_transport_->SetDelegate(delegate);
}

}  // namespace quic
