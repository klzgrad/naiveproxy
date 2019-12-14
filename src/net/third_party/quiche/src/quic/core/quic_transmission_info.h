// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_TRANSMISSION_INFO_H_
#define QUICHE_QUIC_CORE_QUIC_TRANSMISSION_INFO_H_

#include <list>

#include "net/third_party/quiche/src/quic/core/frames/quic_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_ack_listener_interface.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// Stores details of a single sent packet.
struct QUIC_EXPORT_PRIVATE QuicTransmissionInfo {
  // Used by STL when assigning into a map.
  QuicTransmissionInfo();

  // Constructs a Transmission with a new all_transmissions set
  // containing |packet_number|.
  QuicTransmissionInfo(EncryptionLevel level,
                       QuicPacketNumberLength packet_number_length,
                       TransmissionType transmission_type,
                       QuicTime sent_time,
                       QuicPacketLength bytes_sent,
                       bool has_crypto_handshake,
                       int num_padding_bytes);

  QuicTransmissionInfo(const QuicTransmissionInfo& other);

  ~QuicTransmissionInfo();

  QuicFrames retransmittable_frames;
  EncryptionLevel encryption_level;
  // TODO(fayang): remove this when deprecating QUIC_VERSION_39.
  QuicPacketNumberLength packet_number_length;
  QuicPacketLength bytes_sent;
  QuicTime sent_time;
  // Reason why this packet was transmitted.
  TransmissionType transmission_type;
  // In flight packets have not been abandoned or lost.
  bool in_flight;
  // State of this packet.
  SentPacketState state;
  // True if the packet contains stream data from the crypto stream.
  bool has_crypto_handshake;
  // Non-zero if the packet needs padding if it's retransmitted.
  int16_t num_padding_bytes;
  // Stores the packet number of the next retransmission of this packet.
  // Zero if the packet has not been retransmitted.
  // TODO(fayang): rename this to first_sent_after_loss_ when deprecating
  // QUIC_VERSION_41.
  QuicPacketNumber retransmission;
  // The largest_acked in the ack frame, if the packet contains an ack.
  QuicPacketNumber largest_acked;
};
// TODO(ianswett): Add static_assert when size of this struct is reduced below
// 64 bytes.
// NOTE(vlovich): Existing static_assert removed because padding differences on
// 64-bit iOS resulted in an 88-byte struct that is greater than the 84-byte
// limit on other platforms.  Removing per ianswett's request.

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_TRANSMISSION_INFO_H_
