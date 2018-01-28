// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_PENDING_RETRANSMISSION_H_
#define NET_QUIC_CORE_QUIC_PENDING_RETRANSMISSION_H_

#include "net/quic/core/frames/quic_frame.h"
#include "net/quic/core/quic_transmission_info.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

// Struct to store the pending retransmission information.
struct QUIC_EXPORT_PRIVATE QuicPendingRetransmission {
  QuicPendingRetransmission(QuicPacketNumber packet_number,
                            TransmissionType transmission_type,
                            const QuicFrames& retransmittable_frames,
                            bool has_crypto_handshake,
                            int num_padding_bytes,
                            EncryptionLevel encryption_level,
                            QuicPacketNumberLength packet_number_length)
      : packet_number(packet_number),
        retransmittable_frames(retransmittable_frames),
        transmission_type(transmission_type),
        has_crypto_handshake(has_crypto_handshake),
        num_padding_bytes(num_padding_bytes),
        encryption_level(encryption_level),
        packet_number_length(packet_number_length) {}

  QuicPendingRetransmission(QuicPacketNumber packet_number,
                            TransmissionType transmission_type,
                            const QuicTransmissionInfo& tranmission_info)
      : packet_number(packet_number),
        retransmittable_frames(tranmission_info.retransmittable_frames),
        transmission_type(transmission_type),
        has_crypto_handshake(tranmission_info.has_crypto_handshake),
        num_padding_bytes(tranmission_info.num_padding_bytes),
        encryption_level(tranmission_info.encryption_level),
        packet_number_length(tranmission_info.packet_number_length) {}

  QuicPacketNumber packet_number;
  const QuicFrames& retransmittable_frames;
  TransmissionType transmission_type;
  bool has_crypto_handshake;
  int num_padding_bytes;
  EncryptionLevel encryption_level;
  QuicPacketNumberLength packet_number_length;
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_PENDING_RETRANSMISSION_H_
