// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_packet_creator_peer.h"

#include <memory>
#include <string>
#include <utility>

#include "quiche/quic/core/frames/quic_frame.h"
#include "quiche/quic/core/quic_packet_creator.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {
namespace test {

// static
bool QuicPacketCreatorPeer::SendVersionInPacket(QuicPacketCreator* creator) {
  return creator->IncludeVersionInHeader();
}

// static
void QuicPacketCreatorPeer::SetSendVersionInPacket(
    QuicPacketCreator* creator, bool send_version_in_packet) {
  if (!send_version_in_packet) {
    creator->packet_.encryption_level = ENCRYPTION_FORWARD_SECURE;
    return;
  }
  QUICHE_DCHECK(creator->packet_.encryption_level < ENCRYPTION_FORWARD_SECURE);
}

// static
void QuicPacketCreatorPeer::SetPacketNumberLength(
    QuicPacketCreator* creator, QuicPacketNumberLength packet_number_length) {
  creator->packet_.packet_number_length = packet_number_length;
}

// static
QuicPacketNumberLength QuicPacketCreatorPeer::GetPacketNumberLength(
    QuicPacketCreator* creator) {
  return creator->GetPacketNumberLength();
}

// static
quiche::QuicheVariableLengthIntegerLength
QuicPacketCreatorPeer::GetRetryTokenLengthLength(QuicPacketCreator* creator) {
  return creator->GetRetryTokenLengthLength();
}

// static
quiche::QuicheVariableLengthIntegerLength
QuicPacketCreatorPeer::GetLengthLength(QuicPacketCreator* creator) {
  return creator->GetLengthLength();
}

void QuicPacketCreatorPeer::SetPacketNumber(QuicPacketCreator* creator,
                                            uint64_t s) {
  QUICHE_DCHECK_NE(0u, s);
  creator->packet_.packet_number = QuicPacketNumber(s);
}

void QuicPacketCreatorPeer::SetPacketNumber(QuicPacketCreator* creator,
                                            QuicPacketNumber num) {
  creator->packet_.packet_number = num;
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
                                              QuicStreamOffset offset, bool fin,
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
    QuicPacketCreator* creator, const QuicFrames& frames, char* buffer,
    size_t buffer_len) {
  QUICHE_DCHECK(creator->queued_frames_.empty());
  QUICHE_DCHECK(!frames.empty());
  for (const QuicFrame& frame : frames) {
    bool success = creator->AddFrame(frame, NOT_RETRANSMISSION);
    QUICHE_DCHECK(success);
  }
  const bool success =
      creator->SerializePacket(QuicOwnedPacketBuffer(buffer, nullptr),
                               buffer_len, /*allow_padding=*/true);
  QUICHE_DCHECK(success);
  SerializedPacket packet = std::move(creator->packet_);
  // The caller takes ownership of the QuicEncryptedPacket.
  creator->packet_.encrypted_buffer = nullptr;
  return packet;
}

// static
std::unique_ptr<SerializedPacket>
QuicPacketCreatorPeer::SerializeConnectivityProbingPacket(
    QuicPacketCreator* creator) {
  return creator->SerializeConnectivityProbingPacket();
}

// static
std::unique_ptr<SerializedPacket>
QuicPacketCreatorPeer::SerializePathChallengeConnectivityProbingPacket(
    QuicPacketCreator* creator, const QuicPathFrameBuffer& payload) {
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

// static
std::string QuicPacketCreatorPeer::GetRetryToken(QuicPacketCreator* creator) {
  return creator->retry_token_;
}

// static
QuicFrames& QuicPacketCreatorPeer::QueuedFrames(QuicPacketCreator* creator) {
  return creator->queued_frames_;
}

// static
void QuicPacketCreatorPeer::SetRandom(QuicPacketCreator* creator,
                                      QuicRandom* random) {
  creator->random_ = random;
}

}  // namespace test
}  // namespace quic
