// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_CLIENT_H_
#define QUICHE_QUIC_MASQUE_MASQUE_CLIENT_H_

#include <string>

#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/masque/masque_client_session.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/tools/quic_default_client.h"

namespace quic {

// QUIC client that implements MASQUE.
class QUIC_NO_EXPORT MasqueClient : public QuicDefaultClient,
                                    public MasqueClientSession::Owner {
 public:
  // Constructs a MasqueClient, performs a synchronous DNS lookup.
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

 private:
  // Constructor is private, use Create() instead.
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
