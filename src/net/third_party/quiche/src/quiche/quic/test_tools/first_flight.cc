// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/first_flight.h"

#include <memory>
#include <vector>

#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/mock_connection_id_generator.h"
#include "quiche/quic/test_tools/quic_connection_peer.h"
#include "quiche/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {

// Utility class that creates a custom HTTP/3 session and QUIC connection in
// order to extract the first flight of packets it sends. This is meant to only
// be used by GetFirstFlightOfPackets() below.
class FirstFlightExtractor : public DelegatedPacketWriter::Delegate {
 public:
  FirstFlightExtractor(const ParsedQuicVersion& version,
                       const QuicConfig& config,
                       const QuicConnectionId& server_connection_id,
                       const QuicConnectionId& client_connection_id,
                       std::unique_ptr<QuicCryptoClientConfig> crypto_config)
      : version_(version),
        server_connection_id_(server_connection_id),
        client_connection_id_(client_connection_id),
        writer_(this),
        config_(config),
        crypto_config_(std::move(crypto_config)) {
    EXPECT_NE(version_, UnsupportedQuicVersion());
  }

  FirstFlightExtractor(const ParsedQuicVersion& version,
                       const QuicConfig& config,
                       const QuicConnectionId& server_connection_id,
                       const QuicConnectionId& client_connection_id)
      : FirstFlightExtractor(
            version, config, server_connection_id, client_connection_id,
            std::make_unique<QuicCryptoClientConfig>(
                crypto_test_utils::ProofVerifierForTesting())) {}

  void GenerateFirstFlight(QuicEcnCodepoint ecn = ECN_NOT_ECT) {
    crypto_config_->set_alpn(AlpnForVersion(version_));
    connection_ = new QuicConnection(
        server_connection_id_,
        /*initial_self_address=*/QuicSocketAddress(),
        QuicSocketAddress(TestPeerIPAddress(), kTestPort), &connection_helper_,
        &alarm_factory_, &writer_,
        /*owns_writer=*/false, Perspective::IS_CLIENT,
        ParsedQuicVersionVector{version_}, connection_id_generator_);
    if (ecn != ECN_NOT_ECT) {
      QuicConnectionPeer::DisableEcnCodepointValidation(connection_);
      connection_->set_ecn_codepoint(ecn);
    }
    connection_->set_client_connection_id(client_connection_id_);
    session_ = std::make_unique<QuicSpdyClientSession>(
        config_, ParsedQuicVersionVector{version_},
        connection_,  // session_ takes ownership of connection_ here.
        TestServerId(), crypto_config_.get());
    session_->Initialize();
    session_->CryptoConnect();
  }

  void OnDelegatedPacket(const char* buffer, size_t buf_len,
                         const QuicIpAddress& /*self_client_address*/,
                         const QuicSocketAddress& /*peer_client_address*/,
                         PerPacketOptions* /*options*/,
                         const QuicPacketWriterParams& params) override {
    packets_.emplace_back(
        QuicReceivedPacket(buffer, buf_len,
                           connection_helper_.GetClock()->ApproximateNow(),
                           /*owns_buffer=*/false, /*ttl=*/0, /*ttl_valid=*/true,
                           /*packet_headers=*/nullptr, /*headers_length=*/0,
                           /*owns_header_buffer=*/false, params.ecn_codepoint)
            .Clone());
  }

  std::vector<std::unique_ptr<QuicReceivedPacket>>&& ConsumePackets() {
    return std::move(packets_);
  }

  uint64_t GetCryptoStreamBytesWritten() const {
    QUICHE_DCHECK(session_);
    QUICHE_DCHECK(session_->GetCryptoStream());
    return session_->GetCryptoStream()->BytesSentOnLevel(
        EncryptionLevel::ENCRYPTION_INITIAL);
  }

 private:
  ParsedQuicVersion version_;
  QuicConnectionId server_connection_id_;
  QuicConnectionId client_connection_id_;
  MockQuicConnectionHelper connection_helper_;
  MockAlarmFactory alarm_factory_;
  DelegatedPacketWriter writer_;
  QuicConfig config_;
  std::unique_ptr<QuicCryptoClientConfig> crypto_config_;
  QuicConnection* connection_;  // Owned by session_.
  std::unique_ptr<QuicSpdyClientSession> session_;
  std::vector<std::unique_ptr<QuicReceivedPacket>> packets_;
  MockConnectionIdGenerator connection_id_generator_;
};

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config,
    const QuicConnectionId& server_connection_id,
    const QuicConnectionId& client_connection_id,
    std::unique_ptr<QuicCryptoClientConfig> crypto_config,
    QuicEcnCodepoint ecn) {
  FirstFlightExtractor first_flight_extractor(
      version, config, server_connection_id, client_connection_id,
      std::move(crypto_config));
  first_flight_extractor.GenerateFirstFlight(ecn);
  return first_flight_extractor.ConsumePackets();
}

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config,
    const QuicConnectionId& server_connection_id,
    const QuicConnectionId& client_connection_id,
    std::unique_ptr<QuicCryptoClientConfig> crypto_config) {
  return GetFirstFlightOfPackets(version, config, server_connection_id,
                                 client_connection_id, std::move(crypto_config),
                                 ECN_NOT_ECT);
}

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config,
    const QuicConnectionId& server_connection_id,
    const QuicConnectionId& client_connection_id) {
  FirstFlightExtractor first_flight_extractor(
      version, config, server_connection_id, client_connection_id);
  first_flight_extractor.GenerateFirstFlight();
  return first_flight_extractor.ConsumePackets();
}

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config,
    const QuicConnectionId& server_connection_id) {
  return GetFirstFlightOfPackets(version, config, server_connection_id,
                                 EmptyQuicConnectionId());
}

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config) {
  return GetFirstFlightOfPackets(version, config, TestConnectionId());
}

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version,
    const QuicConnectionId& server_connection_id,
    const QuicConnectionId& client_connection_id) {
  return GetFirstFlightOfPackets(version, DefaultQuicConfig(),
                                 server_connection_id, client_connection_id);
}

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version,
    const QuicConnectionId& server_connection_id) {
  return GetFirstFlightOfPackets(version, DefaultQuicConfig(),
                                 server_connection_id, EmptyQuicConnectionId());
}

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version) {
  return GetFirstFlightOfPackets(version, DefaultQuicConfig(),
                                 TestConnectionId());
}

AnnotatedPackets GetAnnotatedFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config,
    const QuicConnectionId& server_connection_id,
    const QuicConnectionId& client_connection_id,
    std::unique_ptr<QuicCryptoClientConfig> crypto_config) {
  FirstFlightExtractor first_flight_extractor(
      version, config, server_connection_id, client_connection_id,
      std::move(crypto_config));
  first_flight_extractor.GenerateFirstFlight();
  return AnnotatedPackets{first_flight_extractor.ConsumePackets(),
                          first_flight_extractor.GetCryptoStreamBytesWritten()};
}

AnnotatedPackets GetAnnotatedFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config) {
  FirstFlightExtractor first_flight_extractor(
      version, config, TestConnectionId(), EmptyQuicConnectionId());
  first_flight_extractor.GenerateFirstFlight();
  return AnnotatedPackets{first_flight_extractor.ConsumePackets(),
                          first_flight_extractor.GetCryptoStreamBytesWritten()};
}

}  // namespace test
}  // namespace quic
