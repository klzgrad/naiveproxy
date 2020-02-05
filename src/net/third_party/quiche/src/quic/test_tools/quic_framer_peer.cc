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
bool QuicFramerPeer::ProcessIetfStreamFrame(QuicFramer* framer,
                                            QuicDataReader* reader,
                                            uint8_t frame_type,
                                            QuicStreamFrame* frame) {
  return framer->ProcessIetfStreamFrame(reader, frame_type, frame);
}

// static
bool QuicFramerPeer::AppendIetfStreamFrame(QuicFramer* framer,
                                           const QuicStreamFrame& frame,
                                           bool last_frame_in_packet,
                                           QuicDataWriter* writer) {
  return framer->AppendIetfStreamFrame(frame, last_frame_in_packet, writer);
}

// static
bool QuicFramerPeer::ProcessCryptoFrame(QuicFramer* framer,
                                        QuicDataReader* reader,
                                        QuicCryptoFrame* frame) {
  return framer->ProcessCryptoFrame(reader, frame);
}

// static
bool QuicFramerPeer::AppendCryptoFrame(QuicFramer* framer,
                                       const QuicCryptoFrame& frame,
                                       QuicDataWriter* writer) {
  return framer->AppendCryptoFrame(frame, writer);
}

// static
bool QuicFramerPeer::ProcessIetfAckFrame(QuicFramer* framer,
                                         QuicDataReader* reader,
                                         uint64_t frame_type,
                                         QuicAckFrame* ack_frame) {
  return framer->ProcessIetfAckFrame(reader, frame_type, ack_frame);
}

// static
bool QuicFramerPeer::AppendIetfAckFrameAndTypeByte(QuicFramer* framer,
                                        const QuicAckFrame& frame,
                                        QuicDataWriter* writer) {
  return framer->AppendIetfAckFrameAndTypeByte(frame, writer);
}
// static
size_t QuicFramerPeer::GetIetfAckFrameSize(QuicFramer* framer,
                                           const QuicAckFrame& frame) {
  return framer->GetIetfAckFrameSize(frame);
}

// static
bool QuicFramerPeer::AppendIetfConnectionCloseFrame(
    QuicFramer* framer,
    const QuicConnectionCloseFrame& frame,
    QuicDataWriter* writer) {
  return framer->AppendIetfConnectionCloseFrame(frame, writer);
}

// static
bool QuicFramerPeer::ProcessIetfConnectionCloseFrame(
    QuicFramer* framer,
    QuicDataReader* reader,
    QuicConnectionCloseType type,
    QuicConnectionCloseFrame* frame) {
  return framer->ProcessIetfConnectionCloseFrame(reader, type, frame);
}

// static
bool QuicFramerPeer::ProcessPathChallengeFrame(QuicFramer* framer,
                                               QuicDataReader* reader,
                                               QuicPathChallengeFrame* frame) {
  return framer->ProcessPathChallengeFrame(reader, frame);
}

// static
bool QuicFramerPeer::ProcessPathResponseFrame(QuicFramer* framer,
                                              QuicDataReader* reader,
                                              QuicPathResponseFrame* frame) {
  return framer->ProcessPathResponseFrame(reader, frame);
}

// static
bool QuicFramerPeer::AppendPathChallengeFrame(
    QuicFramer* framer,
    const QuicPathChallengeFrame& frame,
    QuicDataWriter* writer) {
  return framer->AppendPathChallengeFrame(frame, writer);
}

// static
bool QuicFramerPeer::AppendPathResponseFrame(QuicFramer* framer,
                                             const QuicPathResponseFrame& frame,
                                             QuicDataWriter* writer) {
  return framer->AppendPathResponseFrame(frame, writer);
}

// static
bool QuicFramerPeer::AppendIetfResetStreamFrame(QuicFramer* framer,
                                                const QuicRstStreamFrame& frame,
                                                QuicDataWriter* writer) {
  return framer->AppendIetfResetStreamFrame(frame, writer);
}

// static
bool QuicFramerPeer::ProcessIetfResetStreamFrame(QuicFramer* framer,
                                                 QuicDataReader* reader,
                                                 QuicRstStreamFrame* frame) {
  return framer->ProcessIetfResetStreamFrame(reader, frame);
}

// static
bool QuicFramerPeer::ProcessStopSendingFrame(
    QuicFramer* framer,
    QuicDataReader* reader,
    QuicStopSendingFrame* stop_sending_frame) {
  return framer->ProcessStopSendingFrame(reader, stop_sending_frame);
}

// static
bool QuicFramerPeer::AppendStopSendingFrame(
    QuicFramer* framer,
    const QuicStopSendingFrame& stop_sending_frame,
    QuicDataWriter* writer) {
  return framer->AppendStopSendingFrame(stop_sending_frame, writer);
}

// static
bool QuicFramerPeer::AppendMaxDataFrame(QuicFramer* framer,
                                        const QuicWindowUpdateFrame& frame,
                                        QuicDataWriter* writer) {
  return framer->AppendMaxDataFrame(frame, writer);
}

// static
bool QuicFramerPeer::AppendMaxStreamDataFrame(
    QuicFramer* framer,
    const QuicWindowUpdateFrame& frame,
    QuicDataWriter* writer) {
  return framer->AppendMaxStreamDataFrame(frame, writer);
}

// static
bool QuicFramerPeer::ProcessMaxDataFrame(QuicFramer* framer,
                                         QuicDataReader* reader,
                                         QuicWindowUpdateFrame* frame) {
  return framer->ProcessMaxDataFrame(reader, frame);
}

// static
bool QuicFramerPeer::ProcessMaxStreamDataFrame(QuicFramer* framer,
                                               QuicDataReader* reader,
                                               QuicWindowUpdateFrame* frame) {
  return framer->ProcessMaxStreamDataFrame(reader, frame);
}

// static
bool QuicFramerPeer::AppendMaxStreamsFrame(QuicFramer* framer,
                                           const QuicMaxStreamsFrame& frame,
                                           QuicDataWriter* writer) {
  return framer->AppendMaxStreamsFrame(frame, writer);
}

// static
bool QuicFramerPeer::ProcessMaxStreamsFrame(QuicFramer* framer,
                                            QuicDataReader* reader,
                                            QuicMaxStreamsFrame* frame,
                                            uint64_t frame_type) {
  return framer->ProcessMaxStreamsFrame(reader, frame, frame_type);
}

// static
bool QuicFramerPeer::AppendIetfBlockedFrame(QuicFramer* framer,
                                            const QuicBlockedFrame& frame,
                                            QuicDataWriter* writer) {
  return framer->AppendIetfBlockedFrame(frame, writer);
}

// static
bool QuicFramerPeer::ProcessIetfBlockedFrame(QuicFramer* framer,
                                             QuicDataReader* reader,
                                             QuicBlockedFrame* frame) {
  return framer->ProcessIetfBlockedFrame(reader, frame);
}

// static
bool QuicFramerPeer::AppendStreamBlockedFrame(QuicFramer* framer,
                                              const QuicBlockedFrame& frame,
                                              QuicDataWriter* writer) {
  return framer->AppendStreamBlockedFrame(frame, writer);
}

// static
bool QuicFramerPeer::ProcessStreamBlockedFrame(QuicFramer* framer,
                                               QuicDataReader* reader,
                                               QuicBlockedFrame* frame) {
  return framer->ProcessStreamBlockedFrame(reader, frame);
}

// static
bool QuicFramerPeer::AppendStreamsBlockedFrame(
    QuicFramer* framer,
    const QuicStreamsBlockedFrame& frame,
    QuicDataWriter* writer) {
  return framer->AppendStreamsBlockedFrame(frame, writer);
}

// static
bool QuicFramerPeer::ProcessStreamsBlockedFrame(QuicFramer* framer,
                                                QuicDataReader* reader,
                                                QuicStreamsBlockedFrame* frame,
                                                uint64_t frame_type) {
  return framer->ProcessStreamsBlockedFrame(reader, frame, frame_type);
}

// static
bool QuicFramerPeer::AppendNewConnectionIdFrame(
    QuicFramer* framer,
    const QuicNewConnectionIdFrame& frame,
    QuicDataWriter* writer) {
  return framer->AppendNewConnectionIdFrame(frame, writer);
}

// static
bool QuicFramerPeer::ProcessNewConnectionIdFrame(
    QuicFramer* framer,
    QuicDataReader* reader,
    QuicNewConnectionIdFrame* frame) {
  return framer->ProcessNewConnectionIdFrame(reader, frame);
}

// static
bool QuicFramerPeer::AppendRetireConnectionIdFrame(
    QuicFramer* framer,
    const QuicRetireConnectionIdFrame& frame,
    QuicDataWriter* writer) {
  return framer->AppendRetireConnectionIdFrame(frame, writer);
}

// static
bool QuicFramerPeer::ProcessRetireConnectionIdFrame(
    QuicFramer* framer,
    QuicDataReader* reader,
    QuicRetireConnectionIdFrame* frame) {
  return framer->ProcessRetireConnectionIdFrame(reader, frame);
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
size_t QuicFramerPeer::ComputeFrameLength(
    QuicFramer* framer,
    const QuicFrame& frame,
    bool last_frame_in_packet,
    QuicPacketNumberLength packet_number_length) {
  return framer->ComputeFrameLength(frame, last_frame_in_packet,
                                    packet_number_length);
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
