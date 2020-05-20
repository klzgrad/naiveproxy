// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/masque/masque_encapsulated_epoll_client.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/masque/masque_client_session.h"
#include "net/third_party/quiche/src/quic/masque/masque_encapsulated_client_session.h"
#include "net/third_party/quiche/src/quic/masque/masque_epoll_client.h"
#include "net/third_party/quiche/src/quic/masque/masque_utils.h"

namespace quic {

namespace {

// Custom packet writer that allows getting all of a connection's outgoing
// packets.
class MasquePacketWriter : public QuicPacketWriter {
 public:
  explicit MasquePacketWriter(MasqueEncapsulatedEpollClient* client)
      : client_(client) {}
  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& /*self_address*/,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* /*options*/) override {
    DCHECK(peer_address.IsInitialized());
    QUIC_DVLOG(1) << "MasquePacketWriter trying to write " << buf_len
                  << " bytes to " << peer_address;
    quiche::QuicheStringPiece packet(buffer, buf_len);
    client_->masque_client()->masque_client_session()->SendPacket(
        client_->session()->connection()->client_connection_id(),
        client_->session()->connection()->connection_id(), packet,
        peer_address);
    return WriteResult(WRITE_STATUS_OK, buf_len);
  }

  bool IsWriteBlocked() const override { return false; }

  void SetWritable() override {}

  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& /*peer_address*/) const override {
    return kMasqueMaxEncapsulatedPacketSize;
  }

  bool SupportsReleaseTime() const override { return false; }

  bool IsBatchMode() const override { return false; }
  char* GetNextWriteLocation(
      const QuicIpAddress& /*self_address*/,
      const QuicSocketAddress& /*peer_address*/) override {
    return nullptr;
  }

  WriteResult Flush() override { return WriteResult(WRITE_STATUS_OK, 0); }

 private:
  MasqueEncapsulatedEpollClient* client_;  // Unowned.
};

// Custom network helper that allows injecting a custom packet writer in order
// to get all of a connection's outgoing packets.
class MasqueClientEpollNetworkHelper : public QuicClientEpollNetworkHelper {
 public:
  MasqueClientEpollNetworkHelper(QuicEpollServer* epoll_server,
                                 MasqueEncapsulatedEpollClient* client)
      : QuicClientEpollNetworkHelper(epoll_server, client), client_(client) {}
  QuicPacketWriter* CreateQuicPacketWriter() override {
    return new MasquePacketWriter(client_);
  }

 private:
  MasqueEncapsulatedEpollClient* client_;  // Unowned.
};

}  // namespace

MasqueEncapsulatedEpollClient::MasqueEncapsulatedEpollClient(
    QuicSocketAddress server_address,
    const QuicServerId& server_id,
    QuicEpollServer* epoll_server,
    std::unique_ptr<ProofVerifier> proof_verifier,
    MasqueEpollClient* masque_client)
    : QuicClient(
          server_address,
          server_id,
          MasqueSupportedVersions(),
          MasqueEncapsulatedConfig(),
          epoll_server,
          std::make_unique<MasqueClientEpollNetworkHelper>(epoll_server, this),
          std::move(proof_verifier)),
      masque_client_(masque_client) {}

MasqueEncapsulatedEpollClient::~MasqueEncapsulatedEpollClient() {
  masque_client_->masque_client_session()->UnregisterConnectionId(
      client_connection_id_);
}

std::unique_ptr<QuicSession>
MasqueEncapsulatedEpollClient::CreateQuicClientSession(
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection) {
  QUIC_DLOG(INFO) << "Creating MASQUE encapsulated session for "
                  << connection->connection_id();
  return std::make_unique<MasqueEncapsulatedClientSession>(
      *config(), supported_versions, connection, server_id(), crypto_config(),
      push_promise_index(), masque_client_->masque_client_session());
}

MasqueEncapsulatedClientSession*
MasqueEncapsulatedEpollClient::masque_encapsulated_client_session() {
  return static_cast<MasqueEncapsulatedClientSession*>(QuicClient::session());
}

QuicConnectionId MasqueEncapsulatedEpollClient::GetClientConnectionId() {
  if (client_connection_id_.IsEmpty()) {
    client_connection_id_ = QuicUtils::CreateRandomConnectionId();
    masque_client_->masque_client_session()->RegisterConnectionId(
        client_connection_id_, masque_encapsulated_client_session());
  }
  return client_connection_id_;
}

}  // namespace quic
