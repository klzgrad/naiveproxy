// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_FIRST_FLIGHT_H_
#define QUICHE_QUIC_TEST_TOOLS_FIRST_FLIGHT_H_

#include <memory>
#include <vector>

#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace quic {
namespace test {

// Implementation of QuicPacketWriter that sends all packets to a delegate.
class QUIC_NO_EXPORT DelegatedPacketWriter : public QuicPacketWriter {
 public:
  class QUIC_NO_EXPORT Delegate {
   public:
    virtual ~Delegate() {}
    // Note that |buffer| may be released after this call completes so overrides
    // that want to use the data after the call is complete MUST copy it.
    virtual void OnDelegatedPacket(const char* buffer, size_t buf_len,
                                   const QuicIpAddress& self_client_address,
                                   const QuicSocketAddress& peer_client_address,
                                   PerPacketOptions* options,
                                   const QuicPacketWriterParams& params) = 0;
  };

  // |delegate| MUST be valid for the duration of the DelegatedPacketWriter's
  // lifetime.
  explicit DelegatedPacketWriter(Delegate* delegate) : delegate_(delegate) {
    QUICHE_CHECK_NE(delegate_, nullptr);
  }

  // Overrides for QuicPacketWriter.
  bool IsWriteBlocked() const override { return false; }
  void SetWritable() override {}
  absl::optional<int> MessageTooBigErrorCode() const override {
    return absl::nullopt;
  }
  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& /*peer_address*/) const override {
    return kMaxOutgoingPacketSize;
  }
  bool SupportsReleaseTime() const override { return false; }
  bool IsBatchMode() const override { return false; }
  bool SupportsEcn() const override { return false; }
  QuicPacketBuffer GetNextWriteLocation(
      const QuicIpAddress& /*self_address*/,
      const QuicSocketAddress& /*peer_address*/) override {
    return {nullptr, nullptr};
  }
  WriteResult Flush() override { return WriteResult(WRITE_STATUS_OK, 0); }

  WriteResult WritePacket(const char* buffer, size_t buf_len,
                          const QuicIpAddress& self_client_address,
                          const QuicSocketAddress& peer_client_address,
                          PerPacketOptions* options,
                          const QuicPacketWriterParams& params) override {
    delegate_->OnDelegatedPacket(buffer, buf_len, self_client_address,
                                 peer_client_address, options, params);
    return WriteResult(WRITE_STATUS_OK, buf_len);
  }

 private:
  Delegate* delegate_;  // Unowned.
};

// Returns an array of packets that represent the first flight of a real
// HTTP/3 connection. In most cases, this array will only contain one packet
// that carries the CHLO.
std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config,
    const QuicConnectionId& server_connection_id,
    const QuicConnectionId& client_connection_id,
    std::unique_ptr<QuicCryptoClientConfig> crypto_config);

// Below are various convenience overloads that use default values for the
// omitted parameters:
// |config| = DefaultQuicConfig(),
// |server_connection_id| = TestConnectionId(),
// |client_connection_id| = EmptyQuicConnectionId().
// |crypto_config| =
//     QuicCryptoClientConfig(crypto_test_utils::ProofVerifierForTesting())
std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config,
    const QuicConnectionId& server_connection_id,
    const QuicConnectionId& client_connection_id);

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config,
    const QuicConnectionId& server_connection_id);

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version,
    const QuicConnectionId& server_connection_id,
    const QuicConnectionId& client_connection_id);

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version,
    const QuicConnectionId& server_connection_id);

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config);

std::vector<std::unique_ptr<QuicReceivedPacket>> GetFirstFlightOfPackets(
    const ParsedQuicVersion& version);

// Functions that also provide additional information about the session.
struct AnnotatedPackets {
  std::vector<std::unique_ptr<QuicReceivedPacket>> packets;
  uint64_t crypto_stream_size;
};

AnnotatedPackets GetAnnotatedFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config,
    const QuicConnectionId& server_connection_id,
    const QuicConnectionId& client_connection_id,
    std::unique_ptr<QuicCryptoClientConfig> crypto_config);

AnnotatedPackets GetAnnotatedFirstFlightOfPackets(
    const ParsedQuicVersion& version, const QuicConfig& config);

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_FIRST_FLIGHT_H_
