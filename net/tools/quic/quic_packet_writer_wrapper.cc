// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_packet_writer_wrapper.h"

#include "net/quic/core/quic_types.h"

namespace net {

QuicPacketWriterWrapper::QuicPacketWriterWrapper() {}

QuicPacketWriterWrapper::~QuicPacketWriterWrapper() {}

WriteResult QuicPacketWriterWrapper::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* options) {
  return writer_->WritePacket(buffer, buf_len, self_address, peer_address,
                              options);
}

bool QuicPacketWriterWrapper::IsWriteBlockedDataBuffered() const {
  return writer_->IsWriteBlockedDataBuffered();
}

bool QuicPacketWriterWrapper::IsWriteBlocked() const {
  return writer_->IsWriteBlocked();
}

void QuicPacketWriterWrapper::SetWritable() {
  writer_->SetWritable();
}

QuicByteCount QuicPacketWriterWrapper::GetMaxPacketSize(
    const QuicSocketAddress& peer_address) const {
  return writer_->GetMaxPacketSize(peer_address);
}

void QuicPacketWriterWrapper::set_writer(QuicPacketWriter* writer) {
  writer_.reset(writer);
}

}  // namespace net
