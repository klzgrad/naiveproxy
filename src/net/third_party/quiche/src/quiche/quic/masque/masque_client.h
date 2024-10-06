// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_CLIENT_H_
#define QUICHE_QUIC_MASQUE_MASQUE_CLIENT_H_

#include <memory>
#include <string>

#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/masque/masque_client_session.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_client_default_network_helper.h"
#include "quiche/quic/tools/quic_default_client.h"

namespace quic {

// QUIC client that implements MASQUE.
class QUIC_NO_EXPORT MasqueClient : public QuicDefaultClient,
                                    public MasqueClientSession::Owner {
 public:
  // Constructs an underlying-only MasqueClient, performs a synchronous DNS
  // lookup.
  static std::unique_ptr<MasqueClient> Create(
      const std::string& uri_template, MasqueMode masque_mode,
      QuicEventLoop* event_loop, std::unique_ptr<ProofVerifier> proof_verifier);

  // From QuicClient.
  std::unique_ptr<QuicSession> CreateQuicClientSession(
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection) override;

  // Client session for this client.
  MasqueClientSession* masque_client_session();

  // Convenience accessor for the underlying connection ID.
  QuicConnectionId connection_id();

  // From MasqueClientSession::Owner.
  void OnSettingsReceived() override;

  MasqueMode masque_mode() const { return masque_mode_; }
  std::string uri_template() const { return uri_template_; }

  // Initializes the client, sets properties, connects and waits for settings.
  bool Prepare(QuicByteCount max_packet_size);

 protected:
  // Constructor for when this is only an encapsulated client.
  MasqueClient(QuicSocketAddress server_address, const QuicServerId& server_id,
               QuicEventLoop* event_loop, const QuicConfig& config,
               std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
               std::unique_ptr<ProofVerifier> proof_verifier);
  // Constructor for when this client is both encapsulated and underlying.
  // Should only be used by MasqueEncapsulatedClient.
  MasqueClient(QuicSocketAddress server_address, const QuicServerId& server_id,
               MasqueMode masque_mode, QuicEventLoop* event_loop,
               const QuicConfig& config,
               std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
               std::unique_ptr<ProofVerifier> proof_verifier,
               const std::string& uri_template);

 private:
  // Constructor for when this is only an underlying client.
  // This constructor is private, use Create() instead.
  MasqueClient(QuicSocketAddress server_address, const QuicServerId& server_id,
               MasqueMode masque_mode, QuicEventLoop* event_loop,
               std::unique_ptr<ProofVerifier> proof_verifier,
               const std::string& uri_template);
  // Wait synchronously until we receive the peer's settings. Returns whether
  // they were received.
  bool WaitUntilSettingsReceived();

  std::string authority() const;

  // Disallow copy and assign.
  MasqueClient(const MasqueClient&) = delete;
  MasqueClient& operator=(const MasqueClient&) = delete;

  MasqueMode masque_mode_;
  std::string uri_template_;
  bool settings_received_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_CLIENT_H_
