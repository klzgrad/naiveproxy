// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_EPOLL_CLIENT_H_
#define QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_EPOLL_CLIENT_H_

#include "net/third_party/quiche/src/quic/masque/masque_encapsulated_client_session.h"
#include "net/third_party/quiche/src/quic/masque/masque_epoll_client.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/tools/quic_client.h"

namespace quic {

// QUIC client for QUIC encapsulated in MASQUE.
class QUIC_NO_EXPORT MasqueEncapsulatedEpollClient : public QuicClient {
 public:
  MasqueEncapsulatedEpollClient(QuicSocketAddress server_address,
                                const QuicServerId& server_id,
                                QuicEpollServer* epoll_server,
                                std::unique_ptr<ProofVerifier> proof_verifier,
                                MasqueEpollClient* masque_client);
  ~MasqueEncapsulatedEpollClient() override;

  // Disallow copy and assign.
  MasqueEncapsulatedEpollClient(const MasqueEncapsulatedEpollClient&) = delete;
  MasqueEncapsulatedEpollClient& operator=(
      const MasqueEncapsulatedEpollClient&) = delete;

  // From QuicClient.
  std::unique_ptr<QuicSession> CreateQuicClientSession(
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection) override;

  QuicConnectionId GetClientConnectionId() override;

  // MASQUE client that this client is encapsulated in.
  MasqueEpollClient* masque_client() { return masque_client_; }

  // Client session for this client.
  MasqueEncapsulatedClientSession* masque_encapsulated_client_session();

 private:
  MasqueEpollClient* masque_client_;  // Unowned.
  QuicConnectionId client_connection_id_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_EPOLL_CLIENT_H_
