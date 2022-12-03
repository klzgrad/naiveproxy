// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_encapsulated_client.h"

#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/masque/masque_client.h"
#include "quiche/quic/masque/masque_client_session.h"
#include "quiche/quic/masque/masque_encapsulated_client_session.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/tools/quic_client_default_network_helper.h"

namespace quic {

namespace {

// Custom packet writer that allows getting all of a connection's outgoing
// packets.
class MasquePacketWriter : public QuicPacketWriter {
 public:
  explicit MasquePacketWriter(MasqueEncapsulatedClient* client)
      : client_(client) {}
  WriteResult WritePacket(const char* buffer, size_t buf_len,
                          const QuicIpAddress& /*self_address*/,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* /*options*/) override {
    QUICHE_DCHECK(peer_address.IsInitialized());
    QUIC_DVLOG(1) << "MasquePacketWriter trying to write " << buf_len
                  << " bytes to " << peer_address;
    absl::string_view packet(buffer, buf_len);
    client_->masque_client()->masque_client_session()->SendPacket(
        packet, peer_address, client_->masque_encapsulated_client_session());
    return WriteResult(WRITE_STATUS_OK, buf_len);
  }

  bool IsWriteBlocked() const override { return false; }

  void SetWritable() override {}

  absl::optional<int> MessageTooBigErrorCode() const override {
    return absl::nullopt;
  }

  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& /*peer_address*/) const override {
    return kMasqueMaxEncapsulatedPacketSize;
  }

  bool SupportsReleaseTime() const override { return false; }

  bool IsBatchMode() const override { return false; }
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

MasqueEncapsulatedClient::MasqueEncapsulatedClient(
    QuicSocketAddress server_address, const QuicServerId& server_id,
    QuicEventLoop* event_loop, std::unique_ptr<ProofVerifier> proof_verifier,
    MasqueClient* masque_client)
    : QuicDefaultClient(
          server_address, server_id, MasqueSupportedVersions(),
          MasqueEncapsulatedConfig(), event_loop,
          std::make_unique<MasqueClientDefaultNetworkHelper>(event_loop, this),
          std::move(proof_verifier)),
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
  return std::make_unique<MasqueEncapsulatedClientSession>(
      *config(), supported_versions, connection, server_id(), crypto_config(),
      push_promise_index(), masque_client_->masque_client_session());
}

MasqueEncapsulatedClientSession*
MasqueEncapsulatedClient::masque_encapsulated_client_session() {
  return static_cast<MasqueEncapsulatedClientSession*>(
      QuicDefaultClient::session());
}

}  // namespace quic
