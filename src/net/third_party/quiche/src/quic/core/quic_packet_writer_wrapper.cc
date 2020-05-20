// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packet_writer_wrapper.h"

#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

QuicPacketWriterWrapper::QuicPacketWriterWrapper() = default;

QuicPacketWriterWrapper::~QuicPacketWriterWrapper() {
  unset_writer();
}

WriteResult QuicPacketWriterWrapper::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* options) {
  return writer_->WritePacket(buffer, buf_len, self_address, peer_address,
                              options);
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

bool QuicPacketWriterWrapper::SupportsReleaseTime() const {
  return writer_->SupportsReleaseTime();
}

bool QuicPacketWriterWrapper::IsBatchMode() const {
  return writer_->IsBatchMode();
}

char* QuicPacketWriterWrapper::GetNextWriteLocation(
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address) {
  return writer_->GetNextWriteLocation(self_address, peer_address);
}

WriteResult QuicPacketWriterWrapper::Flush() {
  return writer_->Flush();
}

void QuicPacketWriterWrapper::set_writer(QuicPacketWriter* writer) {
  unset_writer();
  writer_ = writer;
  owns_writer_ = true;
}

void QuicPacketWriterWrapper::set_non_owning_writer(QuicPacketWriter* writer) {
  unset_writer();
  writer_ = writer;
  owns_writer_ = false;
}

void QuicPacketWriterWrapper::unset_writer() {
  if (owns_writer_) {
    delete writer_;
  }

  owns_writer_ = false;
  writer_ = nullptr;
}

}  // namespace quic
