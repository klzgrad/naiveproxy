// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/quic_legacy_version_encapsulator.h"
#include "quic/core/crypto/crypto_handshake_message.h"
#include "quic/core/crypto/crypto_protocol.h"
#include "quic/core/quic_utils.h"
#include "quic/platform/api/quic_bug_tracker.h"
#include "common/platform/api/quiche_text_utils.h"

namespace quic {

QuicLegacyVersionEncapsulator::QuicLegacyVersionEncapsulator(
    QuicPacketBuffer packet_buffer)
    : packet_buffer_(packet_buffer) {}

QuicLegacyVersionEncapsulator::~QuicLegacyVersionEncapsulator() {}

// static
QuicByteCount QuicLegacyVersionEncapsulator::GetMinimumOverhead(
    absl::string_view sni) {
  // The number 52 is the sum of:
  // - Flags (1 byte)
  // - Server Connection ID (8 bytes)
  // - Version (4 bytes)
  // - Packet Number (1 byte)
  // - Message Authentication Hash (12 bytes)
  // - Frame Type (1 byte)
  // - Stream ID (1 byte)
  // - ClientHello tag (4 bytes)
  // - ClientHello num tags (2 bytes)
  // - Padding (2 bytes)
  // - SNI tag (4 bytes)
  // - SNI end offset (4 bytes)
  // - QLVE tag (4 bytes)
  // - QLVE end offset (4 bytes)
  return 52 + sni.length();
}

QuicPacketBuffer QuicLegacyVersionEncapsulator::GetPacketBuffer() {
  return packet_buffer_;
}

void QuicLegacyVersionEncapsulator::OnSerializedPacket(
    SerializedPacket serialized_packet) {
  if (encrypted_length_ != 0) {
    unrecoverable_failure_encountered_ = true;
    QUIC_BUG << "OnSerializedPacket called twice";
    return;
  }
  if (serialized_packet.encrypted_length == 0) {
    unrecoverable_failure_encountered_ = true;
    QUIC_BUG << "OnSerializedPacket called with empty packet";
    return;
  }
  encrypted_length_ = serialized_packet.encrypted_length;
}

void QuicLegacyVersionEncapsulator::OnUnrecoverableError(
    QuicErrorCode error,
    const std::string& error_details) {
  unrecoverable_failure_encountered_ = true;
  QUIC_BUG << "QuicLegacyVersionEncapsulator received error " << error << ": "
           << error_details;
}

bool QuicLegacyVersionEncapsulator::ShouldGeneratePacket(
    HasRetransmittableData /*retransmittable*/,
    IsHandshake /*handshake*/) {
  return true;
}

const QuicFrames
QuicLegacyVersionEncapsulator::MaybeBundleAckOpportunistically() {
  // We do not want to ever include any ACKs here, return an empty array.
  return QuicFrames();
}

SerializedPacketFate QuicLegacyVersionEncapsulator::GetSerializedPacketFate(
    bool /*is_mtu_discovery*/,
    EncryptionLevel /*encryption_level*/) {
  return SEND_TO_WRITER;
}

// static
QuicPacketLength QuicLegacyVersionEncapsulator::Encapsulate(
    absl::string_view sni,
    absl::string_view inner_packet,
    const QuicConnectionId& server_connection_id,
    QuicTime creation_time,
    QuicByteCount outer_max_packet_length,
    char* out) {
  if (outer_max_packet_length > kMaxOutgoingPacketSize) {
    outer_max_packet_length = kMaxOutgoingPacketSize;
  }
  CryptoHandshakeMessage outer_chlo;
  outer_chlo.set_tag(kCHLO);
  outer_chlo.SetStringPiece(kSNI, sni);
  outer_chlo.SetStringPiece(kQLVE, inner_packet);
  const QuicData& serialized_outer_chlo = outer_chlo.GetSerialized();
  QUICHE_DCHECK(!LegacyVersionForEncapsulation().UsesCryptoFrames());
  QUICHE_DCHECK(LegacyVersionForEncapsulation().UsesQuicCrypto());
  QuicStreamFrame outer_stream_frame(
      QuicUtils::GetCryptoStreamId(
          LegacyVersionForEncapsulation().transport_version),
      /*fin=*/false,
      /*offset=*/0, serialized_outer_chlo.AsStringPiece());
  QuicFramer outer_framer(
      ParsedQuicVersionVector{LegacyVersionForEncapsulation()}, creation_time,
      Perspective::IS_CLIENT, kQuicDefaultConnectionIdLength);
  outer_framer.SetInitialObfuscators(server_connection_id);
  char outer_encrypted_packet[kMaxOutgoingPacketSize];
  QuicPacketBuffer outer_packet_buffer(outer_encrypted_packet, nullptr);
  QuicLegacyVersionEncapsulator creator_delegate(outer_packet_buffer);
  QuicPacketCreator outer_creator(server_connection_id, &outer_framer,
                                  &creator_delegate);
  outer_creator.SetMaxPacketLength(outer_max_packet_length);
  outer_creator.set_encryption_level(ENCRYPTION_INITIAL);
  outer_creator.SetTransmissionType(NOT_RETRANSMISSION);
  if (!outer_creator.AddPaddedSavedFrame(QuicFrame(outer_stream_frame),
                                         NOT_RETRANSMISSION)) {
    QUIC_BUG << "Failed to add Legacy Version Encapsulation stream frame "
                "(max packet length is "
             << outer_creator.max_packet_length() << ") " << outer_stream_frame;
    return 0;
  }
  outer_creator.FlushCurrentPacket();
  const QuicPacketLength encrypted_length = creator_delegate.encrypted_length_;
  if (creator_delegate.unrecoverable_failure_encountered_ ||
      encrypted_length == 0) {
    QUIC_BUG << "Failed to perform Legacy Version Encapsulation of "
             << inner_packet.length() << " bytes";
    return 0;
  }
  if (encrypted_length > kMaxOutgoingPacketSize) {
    QUIC_BUG << "Legacy Version Encapsulation outer creator generated a "
                "packet with unexpected length "
             << encrypted_length;
    return 0;
  }

  QUIC_DLOG(INFO) << "Successfully performed Legacy Version Encapsulation from "
                  << inner_packet.length() << " bytes to " << encrypted_length;

  // Replace our current packet with the encapsulated one.
  memcpy(out, outer_encrypted_packet, encrypted_length);
  return encrypted_length;
}

}  // namespace quic
