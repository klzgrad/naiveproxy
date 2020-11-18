// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_framer_peer.h"

#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"

namespace quic {
namespace test {

// static
uint64_t QuicFramerPeer::CalculatePacketNumberFromWire(
    QuicFramer* framer,
    QuicPacketNumberLength packet_number_length,
    QuicPacketNumber last_packet_number,
    uint64_t packet_number) {
  return framer->CalculatePacketNumberFromWire(
      packet_number_length, last_packet_number, packet_number);
}

// static
void QuicFramerPeer::SetLastSerializedServerConnectionId(
    QuicFramer* framer,
    QuicConnectionId server_connection_id) {
  framer->last_serialized_server_connection_id_ = server_connection_id;
}

// static
void QuicFramerPeer::SetLastSerializedClientConnectionId(
    QuicFramer* framer,
    QuicConnectionId client_connection_id) {
  framer->last_serialized_client_connection_id_ = client_connection_id;
}

// static
void QuicFramerPeer::SetLastWrittenPacketNumberLength(
    QuicFramer* framer,
    size_t packet_number_length) {
  framer->last_written_packet_number_length_ = packet_number_length;
}

// static
void QuicFramerPeer::SetLargestPacketNumber(QuicFramer* framer,
                                            QuicPacketNumber packet_number) {
  framer->largest_packet_number_ = packet_number;
}

// static
void QuicFramerPeer::SetPerspective(QuicFramer* framer,
                                    Perspective perspective) {
  framer->perspective_ = perspective;
  framer->infer_packet_header_type_from_version_ =
      perspective == Perspective::IS_CLIENT;
}

// static
void QuicFramerPeer::SwapCrypters(QuicFramer* framer1, QuicFramer* framer2) {
  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; i++) {
    framer1->encrypter_[i].swap(framer2->encrypter_[i]);
    framer1->decrypter_[i].swap(framer2->decrypter_[i]);
  }

  EncryptionLevel framer2_level = framer2->decrypter_level_;
  framer2->decrypter_level_ = framer1->decrypter_level_;
  framer1->decrypter_level_ = framer2_level;
  framer2_level = framer2->alternative_decrypter_level_;
  framer2->alternative_decrypter_level_ = framer1->alternative_decrypter_level_;
  framer1->alternative_decrypter_level_ = framer2_level;

  const bool framer2_latch = framer2->alternative_decrypter_latch_;
  framer2->alternative_decrypter_latch_ = framer1->alternative_decrypter_latch_;
  framer1->alternative_decrypter_latch_ = framer2_latch;
}

// static
QuicEncrypter* QuicFramerPeer::GetEncrypter(QuicFramer* framer,
                                            EncryptionLevel level) {
  return framer->encrypter_[level].get();
}

// static
QuicDecrypter* QuicFramerPeer::GetDecrypter(QuicFramer* framer,
                                            EncryptionLevel level) {
  return framer->decrypter_[level].get();
}

// static
void QuicFramerPeer::SetFirstSendingPacketNumber(QuicFramer* framer,
                                                 uint64_t packet_number) {
  *const_cast<QuicPacketNumber*>(&framer->first_sending_packet_number_) =
      QuicPacketNumber(packet_number);
}

// static
void QuicFramerPeer::SetExpectedServerConnectionIDLength(
    QuicFramer* framer,
    uint8_t expected_server_connection_id_length) {
  *const_cast<uint8_t*>(&framer->expected_server_connection_id_length_) =
      expected_server_connection_id_length;
}

// static
QuicPacketNumber QuicFramerPeer::GetLargestDecryptedPacketNumber(
    QuicFramer* framer,
    PacketNumberSpace packet_number_space) {
  return framer->largest_decrypted_packet_numbers_[packet_number_space];
}

// static
bool QuicFramerPeer::ProcessAndValidateIetfConnectionIdLength(
    QuicDataReader* reader,
    ParsedQuicVersion version,
    Perspective perspective,
    bool should_update_expected_server_connection_id_length,
    uint8_t* expected_server_connection_id_length,
    uint8_t* destination_connection_id_length,
    uint8_t* source_connection_id_length,
    std::string* detailed_error) {
  return QuicFramer::ProcessAndValidateIetfConnectionIdLength(
      reader, version, perspective,
      should_update_expected_server_connection_id_length,
      expected_server_connection_id_length, destination_connection_id_length,
      source_connection_id_length, detailed_error);
}

}  // namespace test
}  // namespace quic
