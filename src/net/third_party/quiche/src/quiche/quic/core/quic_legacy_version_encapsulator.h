// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_LEGACY_VERSION_ENCAPSULATOR_H_
#define QUICHE_QUIC_CORE_QUIC_LEGACY_VERSION_ENCAPSULATOR_H_

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_packet_creator.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// QuicLegacyVersionEncapsulator is responsible for encapsulation of packets
// using Legacy Version Encapsulation.

class QUIC_EXPORT_PRIVATE QuicLegacyVersionEncapsulator
    : public QuicPacketCreator::DelegateInterface {
 public:
  // Encapsulates |inner_packet| into a new encapsulated packet that uses a
  // CHLO of version LegacyVersionForEncapsulation() with server name |sni|
  // exposed and using |server_connection_id|. The packet will be padded up to
  // |outer_max_packet_length| bytes if necessary. On failure, returns 0. On
  // success, returns the length of the outer encapsulated packet, and copies
  // the contents of the encapsulated packet to |out|. |out| must point to a
  // valid memory buffer capable of holding kMaxOutgoingPacketSize bytes.
  static QuicPacketLength Encapsulate(
      absl::string_view sni, absl::string_view inner_packet,
      const QuicConnectionId& server_connection_id, QuicTime creation_time,
      QuicByteCount outer_max_packet_length, char* out);

  // Returns the number of bytes of minimum overhead caused by Legacy Version
  // Encapsulation, based on the length of the provided server name |sni|.
  // The overhead may be higher due to extra padding added.
  static QuicByteCount GetMinimumOverhead(absl::string_view sni);

  // Overrides for QuicPacketCreator::DelegateInterface.
  QuicPacketBuffer GetPacketBuffer() override;
  void OnSerializedPacket(SerializedPacket serialized_packet) override;
  void OnUnrecoverableError(QuicErrorCode error,
                            const std::string& error_details) override;
  bool ShouldGeneratePacket(HasRetransmittableData retransmittable,
                            IsHandshake handshake) override;
  const QuicFrames MaybeBundleAckOpportunistically() override;
  SerializedPacketFate GetSerializedPacketFate(
      bool is_mtu_discovery, EncryptionLevel encryption_level) override;

  ~QuicLegacyVersionEncapsulator() override;

 private:
  explicit QuicLegacyVersionEncapsulator(QuicPacketBuffer packet_buffer);

  // Disallow copy, move and assignment.
  QuicLegacyVersionEncapsulator(const QuicLegacyVersionEncapsulator&) = delete;
  QuicLegacyVersionEncapsulator(QuicLegacyVersionEncapsulator&&) = delete;
  QuicLegacyVersionEncapsulator& operator=(
      const QuicLegacyVersionEncapsulator&) = delete;
  QuicLegacyVersionEncapsulator& operator=(QuicLegacyVersionEncapsulator&&) =
      delete;

  QuicPacketBuffer packet_buffer_;
  QuicPacketLength encrypted_length_ = 0;
  bool unrecoverable_failure_encountered_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_LEGACY_VERSION_ENCAPSULATOR_H_
