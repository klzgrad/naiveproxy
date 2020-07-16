// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_default_packet_writer.h"

#include "net/third_party/quiche/src/quic/core/quic_udp_socket.h"

namespace quic {

QuicDefaultPacketWriter::QuicDefaultPacketWriter(int fd)
    : fd_(fd), write_blocked_(false) {}

QuicDefaultPacketWriter::~QuicDefaultPacketWriter() = default;

WriteResult QuicDefaultPacketWriter::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* options) {
  DCHECK(!write_blocked_);
  DCHECK(nullptr == options)
      << "QuicDefaultPacketWriter does not accept any options.";
  QuicUdpPacketInfo packet_info;
  packet_info.SetPeerAddress(peer_address);
  packet_info.SetSelfIp(self_address);
  WriteResult result =
      QuicUdpSocketApi().WritePacket(fd_, buffer, buf_len, packet_info);
  if (IsWriteBlockedStatus(result.status)) {
    write_blocked_ = true;
  }
  return result;
}

bool QuicDefaultPacketWriter::IsWriteBlocked() const {
  return write_blocked_;
}

void QuicDefaultPacketWriter::SetWritable() {
  write_blocked_ = false;
}

QuicByteCount QuicDefaultPacketWriter::GetMaxPacketSize(
    const QuicSocketAddress& /*peer_address*/) const {
  return kMaxOutgoingPacketSize;
}

bool QuicDefaultPacketWriter::SupportsReleaseTime() const {
  return false;
}

bool QuicDefaultPacketWriter::IsBatchMode() const {
  return false;
}

char* QuicDefaultPacketWriter::GetNextWriteLocation(
    const QuicIpAddress& /*self_address*/,
    const QuicSocketAddress& /*peer_address*/) {
  return nullptr;
}

WriteResult QuicDefaultPacketWriter::Flush() {
  return WriteResult(WRITE_STATUS_OK, 0);
}

void QuicDefaultPacketWriter::set_write_blocked(bool is_blocked) {
  write_blocked_ = is_blocked;
}

}  // namespace quic
