// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_CLIENT_H_
#define QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_CLIENT_H_

#include <memory>

#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/masque/masque_client.h"
#include "quiche/quic/masque/masque_encapsulated_client_session.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_default_client.h"

namespace quic {

// QUIC client for QUIC encapsulated in MASQUE.
class QUIC_NO_EXPORT MasqueEncapsulatedClient : public QuicDefaultClient {
 public:
  MasqueEncapsulatedClient(QuicSocketAddress server_address,
                           const QuicServerId& server_id,
                           QuicEventLoop* event_loop,
                           std::unique_ptr<ProofVerifier> proof_verifier,
                           MasqueClient* masque_client);
  ~MasqueEncapsulatedClient() override;

  // Disallow copy and assign.
  MasqueEncapsulatedClient(const MasqueEncapsulatedClient&) = delete;
  MasqueEncapsulatedClient& operator=(const MasqueEncapsulatedClient&) = delete;

  // From QuicClient.
  std::unique_ptr<QuicSession> CreateQuicClientSession(
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection) override;

  // MASQUE client that this client is encapsulated in.
  MasqueClient* masque_client() { return masque_client_; }

  // Client session for this client.
  MasqueEncapsulatedClientSession* masque_encapsulated_client_session();

 private:
  MasqueClient* masque_client_;  // Unowned.
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_ENCAPSULATED_CLIENT_H_
