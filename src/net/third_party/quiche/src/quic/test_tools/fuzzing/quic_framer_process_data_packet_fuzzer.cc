// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzzer/FuzzedDataProvider.h>
#include <algorithm>
#include <cstdint>
#include <string>

#include "absl/base/macros.h"
#include "quic/core/crypto/null_decrypter.h"
#include "quic/core/crypto/null_encrypter.h"
#include "quic/core/quic_connection_id.h"
#include "quic/core/quic_constants.h"
#include "quic/core/quic_data_writer.h"
#include "quic/core/quic_framer.h"
#include "quic/core/quic_time.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_versions.h"
#include "quic/test_tools/quic_framer_peer.h"
#include "quic/test_tools/quic_test_utils.h"

using quic::DiversificationNonce;
using quic::EncryptionLevel;
using quic::FirstSendingPacketNumber;
using quic::GetPacketHeaderSize;
using quic::kEthernetMTU;
using quic::kQuicDefaultConnectionIdLength;
using quic::NullDecrypter;
using quic::NullEncrypter;
using quic::PacketHeaderFormat;
using quic::ParsedQuicVersion;
using quic::ParsedQuicVersionVector;
using quic::Perspective;
using quic::QuicConnectionId;
using quic::QuicDataReader;
using quic::QuicDataWriter;
using quic::QuicEncryptedPacket;
using quic::QuicFramer;
using quic::QuicFramerVisitorInterface;
using quic::QuicLongHeaderType;
using quic::QuicPacketHeader;
using quic::QuicPacketNumber;
using quic::QuicTime;
using quic::QuicTransportVersion;
using quic::test::NoOpFramerVisitor;
using quic::test::QuicFramerPeer;

PacketHeaderFormat ConsumePacketHeaderFormat(FuzzedDataProvider* provider,
                                             ParsedQuicVersion version) {
  if (!version.HasIetfInvariantHeader()) {
    return quic::GOOGLE_QUIC_PACKET;
  }
  return provider->ConsumeBool() ? quic::IETF_QUIC_LONG_HEADER_PACKET
                                 : quic::IETF_QUIC_SHORT_HEADER_PACKET;
}

ParsedQuicVersion ConsumeParsedQuicVersion(FuzzedDataProvider* provider) {
  // TODO(wub): Add support for v49+.
  const QuicTransportVersion transport_versions[] = {
      quic::QUIC_VERSION_43,
      quic::QUIC_VERSION_46,
  };

  return ParsedQuicVersion(
      quic::PROTOCOL_QUIC_CRYPTO,
      transport_versions[provider->ConsumeIntegralInRange<uint8_t>(
          0, ABSL_ARRAYSIZE(transport_versions) - 1)]);
}

// QuicSelfContainedPacketHeader is a QuicPacketHeader with built-in stroage for
// diversification nonce.
struct QuicSelfContainedPacketHeader : public QuicPacketHeader {
  DiversificationNonce nonce_storage;
};

// Construct a random data packet header that 1) can be successfully serialized
// at sender, and 2) the serialzied buffer can pass the receiver framer's
// ProcessPublicHeader and DecryptPayload functions.
QuicSelfContainedPacketHeader ConsumeQuicPacketHeader(
    FuzzedDataProvider* provider,
    Perspective receiver_perspective) {
  QuicSelfContainedPacketHeader header;

  header.version = ConsumeParsedQuicVersion(provider);

  header.form = ConsumePacketHeaderFormat(provider, header.version);

  const std::string cid_bytes =
      provider->ConsumeBytesAsString(kQuicDefaultConnectionIdLength);
  if (receiver_perspective == Perspective::IS_SERVER) {
    header.destination_connection_id =
        QuicConnectionId(cid_bytes.c_str(), cid_bytes.size());
    header.destination_connection_id_included = quic::CONNECTION_ID_PRESENT;
    header.source_connection_id_included = quic::CONNECTION_ID_ABSENT;
  } else {
    header.source_connection_id =
        QuicConnectionId(cid_bytes.c_str(), cid_bytes.size());
    header.source_connection_id_included = quic::CONNECTION_ID_PRESENT;
    header.destination_connection_id_included = quic::CONNECTION_ID_ABSENT;
  }

  header.version_flag = receiver_perspective == Perspective::IS_SERVER;
  header.reset_flag = false;

  header.packet_number =
      QuicPacketNumber(provider->ConsumeIntegral<uint32_t>());
  if (header.packet_number < FirstSendingPacketNumber()) {
    header.packet_number = FirstSendingPacketNumber();
  }
  header.packet_number_length = quic::PACKET_4BYTE_PACKET_NUMBER;

  header.remaining_packet_length = 0;

  if (header.form != quic::GOOGLE_QUIC_PACKET && header.version_flag) {
    header.long_packet_type = static_cast<QuicLongHeaderType>(
        provider->ConsumeIntegralInRange<uint8_t>(
            // INITIAL, ZERO_RTT_PROTECTED, or HANDSHAKE.
            static_cast<uint8_t>(quic::INITIAL),
            static_cast<uint8_t>(quic::HANDSHAKE)));
  } else {
    header.long_packet_type = quic::INVALID_PACKET_TYPE;
  }

  if (header.form == quic::IETF_QUIC_LONG_HEADER_PACKET &&
      header.long_packet_type == quic::ZERO_RTT_PROTECTED &&
      receiver_perspective == Perspective::IS_CLIENT &&
      header.version.handshake_protocol == quic::PROTOCOL_QUIC_CRYPTO) {
    for (size_t i = 0; i < header.nonce_storage.size(); ++i) {
      header.nonce_storage[i] = provider->ConsumeIntegral<char>();
    }
    header.nonce = &header.nonce_storage;
  } else {
    header.nonce = nullptr;
  }

  return header;
}

void SetupFramer(QuicFramer* framer, QuicFramerVisitorInterface* visitor) {
  framer->set_visitor(visitor);
  for (EncryptionLevel level :
       {quic::ENCRYPTION_INITIAL, quic::ENCRYPTION_HANDSHAKE,
        quic::ENCRYPTION_ZERO_RTT, quic::ENCRYPTION_FORWARD_SECURE}) {
    framer->SetEncrypter(
        level, std::make_unique<NullEncrypter>(framer->perspective()));
    if (framer->version().KnowsWhichDecrypterToUse()) {
      framer->InstallDecrypter(
          level, std::make_unique<NullDecrypter>(framer->perspective()));
    }
  }

  if (!framer->version().KnowsWhichDecrypterToUse()) {
    framer->SetDecrypter(
        quic::ENCRYPTION_INITIAL,
        std::make_unique<NullDecrypter>(framer->perspective()));
  }
}

class FuzzingFramerVisitor : public NoOpFramerVisitor {
 public:
  // Called after a successful ProcessPublicHeader.
  bool OnUnauthenticatedPublicHeader(
      const QuicPacketHeader& /*header*/) override {
    ++process_public_header_success_count_;
    return true;
  }

  // Called after a successful DecryptPayload.
  bool OnPacketHeader(const QuicPacketHeader& /*header*/) override {
    ++decrypted_packet_count_;
    return true;
  }

  uint64_t process_public_header_success_count_ = 0;
  uint64_t decrypted_packet_count_ = 0;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider data_provider(data, size);

  const QuicTime creation_time =
      QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(
                             data_provider.ConsumeIntegral<int32_t>());
  Perspective receiver_perspective = data_provider.ConsumeBool()
                                         ? Perspective::IS_CLIENT
                                         : Perspective::IS_SERVER;
  Perspective sender_perspective =
      (receiver_perspective == Perspective::IS_CLIENT) ? Perspective::IS_SERVER
                                                       : Perspective::IS_CLIENT;

  QuicSelfContainedPacketHeader header =
      ConsumeQuicPacketHeader(&data_provider, receiver_perspective);

  NoOpFramerVisitor sender_framer_visitor;
  ParsedQuicVersionVector framer_versions = {header.version};
  QuicFramer sender_framer(framer_versions, creation_time, sender_perspective,
                           kQuicDefaultConnectionIdLength);
  SetupFramer(&sender_framer, &sender_framer_visitor);

  FuzzingFramerVisitor receiver_framer_visitor;
  QuicFramer receiver_framer(framer_versions, creation_time,
                             receiver_perspective,
                             kQuicDefaultConnectionIdLength);
  SetupFramer(&receiver_framer, &receiver_framer_visitor);
  if (receiver_perspective == Perspective::IS_CLIENT) {
    QuicFramerPeer::SetLastSerializedServerConnectionId(
        &receiver_framer, header.source_connection_id);
  } else {
    QuicFramerPeer::SetLastSerializedClientConnectionId(
        &receiver_framer, header.source_connection_id);
  }

  std::array<char, kEthernetMTU> packet_buffer;
  while (data_provider.remaining_bytes() > 16) {
    const size_t last_remaining_bytes = data_provider.remaining_bytes();

    // Get a randomized packet size.
    uint16_t max_payload_size = static_cast<uint16_t>(
        std::min<size_t>(data_provider.remaining_bytes(), 1350u));
    uint16_t min_payload_size = std::min<uint16_t>(16u, max_payload_size);
    uint16_t payload_size = data_provider.ConsumeIntegralInRange<uint16_t>(
        min_payload_size, max_payload_size);

    QUICHE_CHECK_NE(last_remaining_bytes, data_provider.remaining_bytes())
        << "Check fail to avoid an infinite loop. ConsumeIntegralInRange("
        << min_payload_size << ", " << max_payload_size
        << ") did not consume any bytes. remaining_bytes:"
        << last_remaining_bytes;

    std::vector<char> payload_buffer =
        data_provider.ConsumeBytes<char>(payload_size);
    QUICHE_CHECK_GE(
        packet_buffer.size(),
        GetPacketHeaderSize(sender_framer.transport_version(), header) +
            payload_buffer.size());

    // Serialize the null-encrypted packet into |packet_buffer|.
    QuicDataWriter writer(packet_buffer.size(), packet_buffer.data());
    size_t length_field_offset = 0;
    QUICHE_CHECK(sender_framer.AppendPacketHeader(header, &writer,
                                                  &length_field_offset));

    QUICHE_CHECK(
        writer.WriteBytes(payload_buffer.data(), payload_buffer.size()));

    EncryptionLevel encryption_level =
        quic::test::HeaderToEncryptionLevel(header);
    QUICHE_CHECK(sender_framer.WriteIetfLongHeaderLength(
        header, &writer, length_field_offset, encryption_level));

    size_t encrypted_length = sender_framer.EncryptInPlace(
        encryption_level, header.packet_number,
        GetStartOfEncryptedData(sender_framer.transport_version(), header),
        writer.length(), packet_buffer.size(), packet_buffer.data());
    QUICHE_CHECK_NE(encrypted_length, 0u);

    // Use receiver's framer to process the packet. Ensure both
    // ProcessPublicHeader and DecryptPayload were called and succeeded.
    QuicEncryptedPacket packet(packet_buffer.data(), encrypted_length);
    QuicDataReader reader(packet.data(), packet.length());

    const uint64_t process_public_header_success_count =
        receiver_framer_visitor.process_public_header_success_count_;
    const uint64_t decrypted_packet_count =
        receiver_framer_visitor.decrypted_packet_count_;

    receiver_framer.ProcessPacket(packet);

    QUICHE_DCHECK_EQ(
        process_public_header_success_count + 1,
        receiver_framer_visitor.process_public_header_success_count_)
        << "ProcessPublicHeader failed. error:"
        << QuicErrorCodeToString(receiver_framer.error())
        << ", error_detail:" << receiver_framer.detailed_error()
        << ". header:" << header;
    QUICHE_DCHECK_EQ(decrypted_packet_count + 1,
                     receiver_framer_visitor.decrypted_packet_count_)
        << "Packet was not decrypted. error:"
        << QuicErrorCodeToString(receiver_framer.error())
        << ", error_detail:" << receiver_framer.detailed_error()
        << ". header:" << header;
  }
  return 0;
}
