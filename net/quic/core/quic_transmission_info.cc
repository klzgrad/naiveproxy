// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_transmission_info.h"

namespace net {

QuicTransmissionInfo::QuicTransmissionInfo()
    : encryption_level(ENCRYPTION_NONE),
      packet_number_length(PACKET_1BYTE_PACKET_NUMBER),
      bytes_sent(0),
      sent_time(QuicTime::Zero()),
      transmission_type(NOT_RETRANSMISSION),
      in_flight(false),
      is_unackable(false),
      has_crypto_handshake(false),
      num_padding_bytes(0),
      retransmission(0),
      largest_acked(0) {}

QuicTransmissionInfo::QuicTransmissionInfo(
    EncryptionLevel level,
    QuicPacketNumberLength packet_number_length,
    TransmissionType transmission_type,
    QuicTime sent_time,
    QuicPacketLength bytes_sent,
    bool has_crypto_handshake,
    int num_padding_bytes)
    : encryption_level(level),
      packet_number_length(packet_number_length),
      bytes_sent(bytes_sent),
      sent_time(sent_time),
      transmission_type(transmission_type),
      in_flight(false),
      is_unackable(false),
      has_crypto_handshake(has_crypto_handshake),
      num_padding_bytes(num_padding_bytes),
      retransmission(0),
      largest_acked(0) {}

QuicTransmissionInfo::QuicTransmissionInfo(const QuicTransmissionInfo& other) =
    default;

QuicTransmissionInfo::~QuicTransmissionInfo() {}

}  // namespace net
