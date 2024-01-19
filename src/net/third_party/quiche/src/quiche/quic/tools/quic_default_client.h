// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A toy client, which connects to a specified port and sends QUIC
// request to that endpoint.

#ifndef QUICHE_QUIC_TOOLS_QUIC_DEFAULT_CLIENT_H_
#define QUICHE_QUIC_TOOLS_QUIC_DEFAULT_CLIENT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/tools/quic_client_default_network_helper.h"
#include "quiche/quic/tools/quic_spdy_client_base.h"

namespace quic {

class QuicServerId;

namespace test {
class QuicDefaultClientPeer;
}  // namespace test

class QuicDefaultClient : public QuicSpdyClientBase {
 public:
  // These will create their own QuicClientDefaultNetworkHelper.
  QuicDefaultClient(QuicSocketAddress server_address,
                    const QuicServerId& server_id,
                    const ParsedQuicVersionVector& supported_versions,
                    QuicEventLoop* event_loop,
                    std::unique_ptr<ProofVerifier> proof_verifier);
  QuicDefaultClient(QuicSocketAddress server_address,
                    const QuicServerId& server_id,
                    const ParsedQuicVersionVector& supported_versions,
                    QuicEventLoop* event_loop,
                    std::unique_ptr<ProofVerifier> proof_verifier,
                    std::unique_ptr<SessionCache> session_cache);
  QuicDefaultClient(QuicSocketAddress server_address,
                    const QuicServerId& server_id,
                    const ParsedQuicVersionVector& supported_versions,
                    const QuicConfig& config, QuicEventLoop* event_loop,
                    std::unique_ptr<ProofVerifier> proof_verifier,
                    std::unique_ptr<SessionCache> session_cache);
  // This will take ownership of a passed in network primitive.
  QuicDefaultClient(
      QuicSocketAddress server_address, const QuicServerId& server_id,
      const ParsedQuicVersionVector& supported_versions,
      QuicEventLoop* event_loop,
      std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
      std::unique_ptr<ProofVerifier> proof_verifier);
  QuicDefaultClient(
      QuicSocketAddress server_address, const QuicServerId& server_id,
      const ParsedQuicVersionVector& supported_versions,
      const QuicConfig& config, QuicEventLoop* event_loop,
      std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
      std::unique_ptr<ProofVerifier> proof_verifier);
  QuicDefaultClient(
      QuicSocketAddress server_address, const QuicServerId& server_id,
      const ParsedQuicVersionVector& supported_versions,
      const QuicConfig& config, QuicEventLoop* event_loop,
      std::unique_ptr<QuicClientDefaultNetworkHelper> network_helper,
      std::unique_ptr<ProofVerifier> proof_verifier,
      std::unique_ptr<SessionCache> session_cache);
  QuicDefaultClient(const QuicDefaultClient&) = delete;
  QuicDefaultClient& operator=(const QuicDefaultClient&) = delete;

  ~QuicDefaultClient() override;

  // QuicSpdyClientBase overrides.
  std::unique_ptr<QuicSession> CreateQuicClientSession(
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection) override;

  // Exposed for QUIC tests.
  int GetLatestFD() const { return default_network_helper()->GetLatestFD(); }

  QuicClientDefaultNetworkHelper* default_network_helper();
  const QuicClientDefaultNetworkHelper* default_network_helper() const;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_DEFAULT_CLIENT_H_
