// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/quic_packet_creator_peer.h"

#include "net/quic/core/quic_packet_creator.h"

namespace net {
namespace test {

// static
bool QuicPacketCreatorPeer::SendVersionInPacket(QuicPacketCreator* creator) {
  return creator->send_version_in_packet_;
}

// static
void QuicPacketCreatorPeer::SetSendVersionInPacket(
    QuicPacketCreator* creator,
    bool send_version_in_packet) {
  creator->send_version_in_packet_ = send_version_in_packet;
}

// static
void QuicPacketCreatorPeer::SetPacketNumberLength(
    QuicPacketCreator* creator,
    QuicPacketNumberLength packet_number_length) {
  creator->packet_.packet_number_length = packet_number_length;
}

// static
QuicPacketNumberLength QuicPacketCreatorPeer::GetPacketNumberLength(
    QuicPacketCreator* creator) {
  return creator->packet_.packet_number_length;
}

void QuicPacketCreatorPeer::SetPacketNumber(QuicPacketCreator* creator,
                                            QuicPacketNumber s) {
  creator->packet_.packet_number = s;
}

// static
void QuicPacketCreatorPeer::FillPacketHeader(QuicPacketCreator* creator,
                                             QuicPacketHeader* header) {
  creator->FillPacketHeader(header);
}

// static
void QuicPacketCreatorPeer::CreateStreamFrame(QuicPacketCreator* creator,
                                              QuicStreamId id,
                                              size_t write_length,
                                              size_t iov_offset,
                                              QuicStreamOffset offset,
                                              bool fin,
                                              QuicFrame* frame) {
  creator->CreateStreamFrame(id, write_length, iov_offset, offset, fin, frame);
}

// static
SerializedPacket QuicPacketCreatorPeer::SerializeAllFrames(
    QuicPacketCreator* creator,
    const QuicFrames& frames,
    char* buffer,
    size_t buffer_len) {
  DCHECK(creator->queued_frames_.empty());
  DCHECK(!frames.empty());
  for (const QuicFrame& frame : frames) {
    bool success = creator->AddFrame(frame, false);
    DCHECK(success);
  }
  creator->SerializePacket(buffer, buffer_len);
  SerializedPacket packet = creator->packet_;
  // The caller takes ownership of the QuicEncryptedPacket.
  creator->packet_.encrypted_buffer = nullptr;
  DCHECK(packet.retransmittable_frames.empty());
  return packet;
}

// static
std::unique_ptr<QuicEncryptedPacket>
QuicPacketCreatorPeer::SerializeConnectivityProbingPacket(
    QuicPacketCreator* creator) {
  return creator->SerializeConnectivityProbingPacket();
}

// static
EncryptionLevel QuicPacketCreatorPeer::GetEncryptionLevel(
    QuicPacketCreator* creator) {
  return creator->packet_.encryption_level;
}

// static
QuicFramer* QuicPacketCreatorPeer::framer(QuicPacketCreator* creator) {
  return creator->framer_;
}

}  // namespace test
}  // namespace net
