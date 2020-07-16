// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_FRAMER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_FRAMER_PEER_H_

#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"

namespace quic {

namespace test {

class QuicFramerPeer {
 public:
  QuicFramerPeer() = delete;

  static uint64_t CalculatePacketNumberFromWire(
      QuicFramer* framer,
      QuicPacketNumberLength packet_number_length,
      QuicPacketNumber last_packet_number,
      uint64_t packet_number);
  static void SetLastSerializedServerConnectionId(
      QuicFramer* framer,
      QuicConnectionId server_connection_id);
  static void SetLastSerializedClientConnectionId(
      QuicFramer* framer,
      QuicConnectionId client_connection_id);
  static void SetLastWrittenPacketNumberLength(QuicFramer* framer,
                                               size_t packet_number_length);
  static void SetLargestPacketNumber(QuicFramer* framer,
                                     QuicPacketNumber packet_number);
  static void SetPerspective(QuicFramer* framer, Perspective perspective);

  // SwapCrypters exchanges the state of the crypters of |framer1| with
  // |framer2|.
  static void SwapCrypters(QuicFramer* framer1, QuicFramer* framer2);

  static QuicEncrypter* GetEncrypter(QuicFramer* framer, EncryptionLevel level);
  static QuicDecrypter* GetDecrypter(QuicFramer* framer, EncryptionLevel level);

  static void SetFirstSendingPacketNumber(QuicFramer* framer,
                                          uint64_t packet_number);
  static void SetExpectedServerConnectionIDLength(
      QuicFramer* framer,
      uint8_t expected_server_connection_id_length);
  static QuicPacketNumber GetLargestDecryptedPacketNumber(
      QuicFramer* framer,
      PacketNumberSpace packet_number_space);

  static bool ProcessAndValidateIetfConnectionIdLength(
      QuicDataReader* reader,
      ParsedQuicVersion version,
      Perspective perspective,
      bool should_update_expected_server_connection_id_length,
      uint8_t* expected_server_connection_id_length,
      uint8_t* destination_connection_id_length,
      uint8_t* source_connection_id_length,
      std::string* detailed_error);

  static void set_current_received_frame_type(
      QuicFramer* framer,
      uint64_t current_received_frame_type) {
    framer->current_received_frame_type_ = current_received_frame_type;
  }

  static bool infer_packet_header_type_from_version(QuicFramer* framer) {
    return framer->infer_packet_header_type_from_version_;
  }
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_FRAMER_PEER_H_
