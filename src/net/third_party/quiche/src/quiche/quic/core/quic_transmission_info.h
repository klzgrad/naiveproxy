// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_TRANSMISSION_INFO_H_
#define QUICHE_QUIC_CORE_QUIC_TRANSMISSION_INFO_H_

#include <list>

#include "quiche/quic/core/frames/quic_frame.h"
#include "quiche/quic/core/quic_ack_listener_interface.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Stores details of a single sent packet.
struct QUIC_EXPORT_PRIVATE QuicTransmissionInfo {
  // Used by STL when assigning into a map.
  QuicTransmissionInfo();

  // Constructs a Transmission with a new all_transmissions set
  // containing |packet_number|.
  QuicTransmissionInfo(EncryptionLevel level,
                       TransmissionType transmission_type, QuicTime sent_time,
                       QuicPacketLength bytes_sent, bool has_crypto_handshake,
                       bool has_ack_frequency);

  QuicTransmissionInfo(const QuicTransmissionInfo& other);

  ~QuicTransmissionInfo();

  std::string DebugString() const;

  QuicFrames retransmittable_frames;
  QuicTime sent_time;
  QuicPacketLength bytes_sent;
  EncryptionLevel encryption_level;
  // Reason why this packet was transmitted.
  TransmissionType transmission_type;
  // In flight packets have not been abandoned or lost.
  bool in_flight;
  // State of this packet.
  SentPacketState state;
  // True if the packet contains stream data from the crypto stream.
  bool has_crypto_handshake;
  // True if the packet contains ack frequency frame.
  bool has_ack_frequency;
  // Records the first sent packet after this packet was detected lost. Zero if
  // this packet has not been detected lost. This is used to keep lost packet
  // for another RTT (for potential spurious loss detection)
  QuicPacketNumber first_sent_after_loss;
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
