// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_encapsulated_client.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/masque/masque_client.h"
#include "quiche/quic/masque/masque_client_session.h"
#include "quiche/quic/masque/masque_encapsulated_client_session.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_client_default_network_helper.h"
#include "quiche/quic/tools/quic_default_client.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"

namespace quic {

namespace {

class ChecksumWriter {
 public:
  explicit ChecksumWriter(quiche::QuicheDataWriter& writer) : writer_(writer) {}
  void IngestUInt16(uint16_t val) { accumulator_ += val; }
  void IngestUInt8(uint8_t val) {
    uint16_t val16 = odd_ ? val : (val << 8);
    accumulator_ += val16;
    odd_ = !odd_;
  }
  bool IngestData(size_t offset, size_t length) {
    quiche::QuicheDataReader reader(
        writer_.data(), std::min<size_t>(offset + length, writer_.capacity()));
    if (!reader.Seek(offset) || reader.BytesRemaining() < length) {
      return false;
    }
    // Handle any potentially off first byte.
    uint8_t first_byte;
    if (odd_ && reader.ReadUInt8(&first_byte)) {
      IngestUInt8(first_byte);
    }
    // Handle each 16-bit word at a time.
    while (reader.BytesRemaining() >= sizeof(uint16_t)) {
      uint16_t word;
      if (!reader.ReadUInt16(&word)) {
        return false;
      }
      IngestUInt16(word);
    }
    // Handle any leftover odd byte.
    uint8_t last_byte;
    if (reader.ReadUInt8(&last_byte)) {
      IngestUInt8(last_byte);
    }
    return true;
  }
  bool WriteChecksumAtOffset(size_t offset) {
    while (accumulator_ >> 16 > 0) {
      accumulator_ = (accumulator_ & 0xffff) + (accumulator_ >> 16);
    }
    accumulator_ = 0xffff & ~accumulator_;
    quiche::QuicheDataWriter writer2(writer_.capacity(), writer_.data());
    return writer2.Seek(offset) && writer2.WriteUInt16(accumulator_);
  }

 private:
  quiche::QuicheDataWriter& writer_;
  uint32_t accumulator_ = 0xffff;
  bool odd_ = false;
};

// Custom packet writer that allows getting all of a connection's outgoing
// packets.
class MasquePacketWriter : public QuicPacketWriter {
 public:
  explicit MasquePacketWriter(MasqueEncapsulatedClient* client)
      : client_(client) {}
  WriteResult WritePacket(const char* buffer, size_t buf_len,
                          const QuicIpAddress& /*self_address*/,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* /*options*/,
                          const QuicPacketWriterParams& /*params*/) override {
    QUICHE_DCHECK(peer_address.IsInitialized());
    QUIC_DVLOG(1) << "MasquePacketWriter trying to write " << buf_len
                  << " bytes to " << peer_address;
    if (client_->masque_client()->masque_mode() == MasqueMode::kConnectIp) {
      constexpr size_t kIPv4HeaderSize = 20;
      constexpr size_t kIPv4ChecksumOffset = 10;
      constexpr size_t kIPv6HeaderSize = 40;
      constexpr size_t kUdpHeaderSize = 8;
      const size_t udp_length = kUdpHeaderSize + buf_len;
      std::string packet;
      packet.resize(
          (peer_address.host().IsIPv6() ? kIPv6HeaderSize : kIPv4HeaderSize) +
          udp_length);
      quiche::QuicheDataWriter writer(packet.size(), packet.data());
      if (peer_address.host().IsIPv6()) {
        // Write IPv6 header.
        QUICHE_CHECK(writer.WriteUInt8(0x60));  // Version = 6 and DSCP.
        QUICHE_CHECK(writer.WriteUInt8(0));     // DSCP/ECN and flow label.
        QUICHE_CHECK(writer.WriteUInt16(0));    // Flow label.
        QUICHE_CHECK(writer.WriteUInt16(udp_length));  // Payload Length.
        QUICHE_CHECK(writer.WriteUInt8(17));           // Next header = UDP.
        QUICHE_CHECK(writer.WriteUInt8(64));           // Hop limit = 64.
        in6_addr source_address = {};
        if (client_->masque_encapsulated_client_session()
                ->local_v6_address()
                .IsIPv6()) {
          source_address = client_->masque_encapsulated_client_session()
                               ->local_v6_address()
                               .GetIPv6();
        }
        QUICHE_CHECK(
            writer.WriteBytes(&source_address, sizeof(source_address)));
        in6_addr destination_address = peer_address.host().GetIPv6();
        QUICHE_CHECK(writer.WriteBytes(&destination_address,
                                       sizeof(destination_address)));
      } else {
        // Write IPv4 header.
        QUICHE_CHECK(writer.WriteUInt8(0x45));  // Version = 4, IHL = 5.
        QUICHE_CHECK(writer.WriteUInt8(0));     // DSCP/ECN.
        QUICHE_CHECK(writer.WriteUInt16(packet.size()));  // Total Length.
        QUICHE_CHECK(writer.WriteUInt32(0));              // No fragmentation.
        QUICHE_CHECK(writer.WriteUInt8(64));              // TTL = 64.
        QUICHE_CHECK(writer.WriteUInt8(17));              // IP Protocol = UDP.
        QUICHE_CHECK(writer.WriteUInt16(0));  // Checksum = 0 initially.
        in_addr source_address = {};
        if (client_->masque_encapsulated_client_session()
                ->local_v4_address()
                .IsIPv4()) {
          source_address = client_->masque_encapsulated_client_session()
                               ->local_v4_address()
                               .GetIPv4();
        }
        QUICHE_CHECK(
            writer.WriteBytes(&source_address, sizeof(source_address)));
        in_addr destination_address = peer_address.host().GetIPv4();
        QUICHE_CHECK(writer.WriteBytes(&destination_address,
                                       sizeof(destination_address)));
        ChecksumWriter ip_checksum_writer(writer);
        QUICHE_CHECK(ip_checksum_writer.IngestData(0, kIPv4HeaderSize));
        QUICHE_CHECK(
            ip_checksum_writer.WriteChecksumAtOffset(kIPv4ChecksumOffset));
      }
      // Write UDP header.
      QUICHE_CHECK(writer.WriteUInt16(0x1234));  // Source port.
      QUICHE_CHECK(
          writer.WriteUInt16(peer_address.port()));  // Destination port.
      QUICHE_CHECK(writer.WriteUInt16(udp_length));  // UDP length.
      QUICHE_CHECK(writer.WriteUInt16(0));           // Checksum = 0 initially.
      // Write UDP payload.
      QUICHE_CHECK(writer.WriteBytes(buffer, buf_len));
      ChecksumWriter udp_checksum_writer(writer);
      if (peer_address.host().IsIPv6()) {
        QUICHE_CHECK(udp_checksum_writer.IngestData(8, 32));  // IP addresses.
        udp_checksum_writer.IngestUInt16(0);  // High bits of UDP length.
        udp_checksum_writer.IngestUInt16(
            udp_length);                      // Low bits of UDP length.
        udp_checksum_writer.IngestUInt16(0);  // Zeroes.
        udp_checksum_writer.IngestUInt8(0);   // Zeroes.
        udp_checksum_writer.IngestUInt8(17);  // Next header = UDP.
        QUICHE_CHECK(udp_checksum_writer.IngestData(
            kIPv6HeaderSize, udp_length));  // UDP header and data.
        QUICHE_CHECK(
            udp_checksum_writer.WriteChecksumAtOffset(kIPv6HeaderSize + 6));
      } else {
        QUICHE_CHECK(udp_checksum_writer.IngestData(12, 8));  // IP addresses.
        udp_checksum_writer.IngestUInt8(0);                   // Zeroes.
        udp_checksum_writer.IngestUInt8(17);           // IP Protocol = UDP.
        udp_checksum_writer.IngestUInt16(udp_length);  // UDP length.
        QUICHE_CHECK(udp_checksum_writer.IngestData(
            kIPv4HeaderSize, udp_length));  // UDP header and data.
        QUICHE_CHECK(
            udp_checksum_writer.WriteChecksumAtOffset(kIPv4HeaderSize + 6));
      }
      client_->masque_client()->masque_client_session()->SendIpPacket(
          packet, client_->masque_encapsulated_client_session());
    } else {
      absl::string_view packet(buffer, buf_len);
      client_->masque_client()->masque_client_session()->SendPacket(
          packet, peer_address, client_->masque_encapsulated_client_session());
    }
    return WriteResult(WRITE_STATUS_OK, buf_len);
  }

  bool IsWriteBlocked() const override { return false; }

  void SetWritable() override {}

  std::optional<int> MessageTooBigErrorCode() const override {
    return std::nullopt;
  }

  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& /*peer_address*/) const override {
    // This is only used as a min against the other limits, so we set it to the
    // maximum value so it doesn't reduce the MTU.
    return kDefaultMaxPacketSizeForTunnels;
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

 private:
  MasqueEncapsulatedClient* client_;  // Unowned.
};

// Custom network helper that allows injecting a custom packet writer in order
// to get all of a connection's outgoing packets.
class MasqueClientDefaultNetworkHelper : public QuicClientDefaultNetworkHelper {
 public:
  MasqueClientDefaultNetworkHelper(QuicEventLoop* event_loop,
                                   MasqueEncapsulatedClient* client)
      : QuicClientDefaultNetworkHelper(event_loop, client), client_(client) {}
  QuicPacketWriter* CreateQuicPacketWriter() override {
    return new MasquePacketWriter(client_);
  }

 private:
  MasqueEncapsulatedClient* client_;  // Unowned.
};

}  // namespace

// static
std::unique_ptr<MasqueEncapsulatedClient> MasqueEncapsulatedClient::Create(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    const std::string& uri_template, MasqueMode masque_mode,
    QuicEventLoop* event_loop, std::unique_ptr<ProofVerifier> proof_verifier,
    MasqueClient* underlying_masque_client) {
  // Use absl::WrapUnique instead of std::make_unique because constructor is
  // private and therefore not accessible from make_unique.
  auto masque_client = absl::WrapUnique(new MasqueEncapsulatedClient(
      server_address, server_id, masque_mode, event_loop,
      std::move(proof_verifier), underlying_masque_client, uri_template));

  if (masque_client == nullptr) {
    QUIC_LOG(ERROR) << "Failed to create masque_client";
    return nullptr;
  }
  if (!masque_client->Prepare(
          MaxPacketSizeForEncapsulatedConnections(underlying_masque_client))) {
    return nullptr;
  }
  return masque_client;
}

MasqueEncapsulatedClient::MasqueEncapsulatedClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    QuicEventLoop* event_loop, std::unique_ptr<ProofVerifier> proof_verifier,
    MasqueClient* masque_client)
    : MasqueClient(
          server_address, server_id, event_loop,
          MasqueEncapsulatedConfig(masque_client),
          std::make_unique<MasqueClientDefaultNetworkHelper>(event_loop, this),
          std::move(proof_verifier)),
      masque_client_(masque_client) {}

MasqueEncapsulatedClient::MasqueEncapsulatedClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    MasqueMode masque_mode, QuicEventLoop* event_loop,
    std::unique_ptr<ProofVerifier> proof_verifier, MasqueClient* masque_client,
    const std::string& uri_template)
    : MasqueClient(
          server_address, server_id, masque_mode, event_loop,
          MasqueEncapsulatedConfig(masque_client),
          std::make_unique<MasqueClientDefaultNetworkHelper>(event_loop, this),
          std::move(proof_verifier), uri_template),
      masque_client_(masque_client) {}

MasqueEncapsulatedClient::~MasqueEncapsulatedClient() {
  masque_client_->masque_client_session()->CloseConnectUdpStream(
      masque_encapsulated_client_session());
}

std::unique_ptr<QuicSession> MasqueEncapsulatedClient::CreateQuicClientSession(
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  QUIC_DLOG(INFO) << "Creating MASQUE encapsulated session for "
                  << connection->connection_id();
  if (!uri_template().empty()) {
    return std::make_unique<MasqueEncapsulatedClientSession>(
        masque_mode(), uri_template(), *config(), supported_versions,
        connection, server_id(), crypto_config(),
        masque_client_->masque_client_session(), this);
  }
  return std::make_unique<MasqueEncapsulatedClientSession>(
      *config(), supported_versions, connection, server_id(), crypto_config(),
      masque_client_->masque_client_session(), this);
}

MasqueEncapsulatedClientSession*
MasqueEncapsulatedClient::masque_encapsulated_client_session() {
  return static_cast<MasqueEncapsulatedClientSession*>(
      QuicDefaultClient::session());
}

QuicByteCount MaxPacketSizeForEncapsulatedConnections(
    MasqueClient* underlying_masque_client) {
  QuicByteCount max_packet_size =
      underlying_masque_client->masque_client_session()
          ->GetGuaranteedLargestMessagePayload() -
      /* max length of quarter stream ID */ sizeof(QuicStreamId) -
      /* context ID set to zero */ sizeof(uint8_t);
  QUICHE_CHECK_GE(max_packet_size, 1200u)
      << "RFC 9000 requires QUIC max packet size to be above 1200 bytes";
  return max_packet_size;
}

QuicConfig MasqueEncapsulatedConfig(MasqueClient* underlying_masque_client) {
  QuicConfig config;
  config.SetMaxPacketSizeToSend(
      MaxPacketSizeForEncapsulatedConnections(underlying_masque_client));
  return config;
}

}  // namespace quic
