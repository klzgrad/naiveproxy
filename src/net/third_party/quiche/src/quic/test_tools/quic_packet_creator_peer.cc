// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_packet_creator_peer.h"

#include "net/third_party/quiche/src/quic/core/quic_packet_creator.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {
namespace test {

// static
bool QuicPacketCreatorPeer::SendVersionInPacket(QuicPacketCreator* creator) {
  return creator->IncludeVersionInHeader();
}

// static
void QuicPacketCreatorPeer::SetSendVersionInPacket(
    QuicPacketCreator* creator,
    bool send_version_in_packet) {
  ParsedQuicVersion version = creator->framer_->version();
  if (!VersionHasIetfQuicFrames(version.transport_version) &&
      version.handshake_protocol != PROTOCOL_TLS1_3) {
    creator->send_version_in_packet_ = send_version_in_packet;
    return;
  }
  if (!send_version_in_packet) {
    creator->packet_.encryption_level = ENCRYPTION_FORWARD_SECURE;
    return;
  }
  DCHECK(creator->packet_.encryption_level < ENCRYPTION_FORWARD_SECURE);
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
  return creator->GetPacketNumberLength();
}

// static
QuicVariableLengthIntegerLength
QuicPacketCreatorPeer::GetRetryTokenLengthLength(QuicPacketCreator* creator) {
  return creator->GetRetryTokenLengthLength();
}

// static
QuicVariableLengthIntegerLength QuicPacketCreatorPeer::GetLengthLength(
    QuicPacketCreator* creator) {
  return creator->GetLengthLength();
}

void QuicPacketCreatorPeer::SetPacketNumber(QuicPacketCreator* creator,
                                            uint64_t s) {
  DCHECK_NE(0u, s);
  creator->packet_.packet_number = QuicPacketNumber(s);
}

// static
void QuicPacketCreatorPeer::ClearPacketNumber(QuicPacketCreator* creator) {
  creator->packet_.packet_number.Clear();
}

// static
void QuicPacketCreatorPeer::FillPacketHeader(QuicPacketCreator* creator,
                                             QuicPacketHeader* header) {
  creator->FillPacketHeader(header);
}

// static
void QuicPacketCreatorPeer::CreateStreamFrame(QuicPacketCreator* creator,
                                              QuicStreamId id,
                                              size_t data_length,
                                              QuicStreamOffset offset,
                                              bool fin,
                                              QuicFrame* frame) {
  creator->CreateStreamFrame(id, data_length, offset, fin, frame);
}

// static
bool QuicPacketCreatorPeer::CreateCryptoFrame(QuicPacketCreator* creator,
                                              EncryptionLevel level,
                                              size_t write_length,
                                              QuicStreamOffset offset,
                                              QuicFrame* frame) {
  return creator->CreateCryptoFrame(level, write_length, offset, frame);
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
    bool success = creator->AddFrame(frame, false, NOT_RETRANSMISSION);
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
OwningSerializedPacketPointer
QuicPacketCreatorPeer::SerializeConnectivityProbingPacket(
    QuicPacketCreator* creator) {
  return creator->SerializeConnectivityProbingPacket();
}

// static
OwningSerializedPacketPointer
QuicPacketCreatorPeer::SerializePathChallengeConnectivityProbingPacket(
    QuicPacketCreator* creator,
    QuicPathFrameBuffer* payload) {
  return creator->SerializePathChallengeConnectivityProbingPacket(payload);
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
}  // namespace quic
