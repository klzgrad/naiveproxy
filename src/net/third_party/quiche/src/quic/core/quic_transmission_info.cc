// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_transmission_info.h"

namespace quic {

QuicTransmissionInfo::QuicTransmissionInfo()
    : encryption_level(ENCRYPTION_INITIAL),
      bytes_sent(0),
      sent_time(QuicTime::Zero()),
      transmission_type(NOT_RETRANSMISSION),
      in_flight(false),
      state(OUTSTANDING),
      has_crypto_handshake(false),
      has_ack_frequency(false) {}

QuicTransmissionInfo::QuicTransmissionInfo(EncryptionLevel level,
                                           TransmissionType transmission_type,
                                           QuicTime sent_time,
                                           QuicPacketLength bytes_sent,
                                           bool has_crypto_handshake,
                                           bool has_ack_frequency)
    : encryption_level(level),
      bytes_sent(bytes_sent),
      sent_time(sent_time),
      transmission_type(transmission_type),
      in_flight(false),
      state(OUTSTANDING),
      has_crypto_handshake(has_crypto_handshake),
      has_ack_frequency(has_ack_frequency) {}

QuicTransmissionInfo::QuicTransmissionInfo(const QuicTransmissionInfo& other) =
    default;

QuicTransmissionInfo::~QuicTransmissionInfo() {}

}  // namespace quic
