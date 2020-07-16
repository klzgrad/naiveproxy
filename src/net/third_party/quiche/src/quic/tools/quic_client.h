// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A toy client, which connects to a specified port and sends QUIC
// request to that endpoint.

#ifndef QUICHE_QUIC_TOOLS_QUIC_CLIENT_H_
#define QUICHE_QUIC_TOOLS_QUIC_CLIENT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/http/quic_client_push_promise_index.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_session.h"
#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_reader.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_epoll.h"
#include "net/third_party/quiche/src/quic/tools/quic_client_epoll_network_helper.h"
#include "net/third_party/quiche/src/quic/tools/quic_spdy_client_base.h"

namespace quic {

class QuicServerId;

namespace test {
class QuicClientPeer;
}  // namespace test

namespace tools {

QuicSocketAddress LookupAddress(std::string host, std::string port);

}  // namespace tools

class QuicClient : public QuicSpdyClientBase {
 public:
  // These will create their own QuicClientEpollNetworkHelper.
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const ParsedQuicVersionVector& supported_versions,
             QuicEpollServer* epoll_server,
             std::unique_ptr<ProofVerifier> proof_verifier);
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const ParsedQuicVersionVector& supported_versions,
             QuicEpollServer* epoll_server,
             std::unique_ptr<ProofVerifier> proof_verifier,
             std::unique_ptr<SessionCache> session_cache);
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const ParsedQuicVersionVector& supported_versions,
             const QuicConfig& config,
             QuicEpollServer* epoll_server,
             std::unique_ptr<ProofVerifier> proof_verifier,
             std::unique_ptr<SessionCache> session_cache);
  // This will take ownership of a passed in network primitive.
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const ParsedQuicVersionVector& supported_versions,
             QuicEpollServer* epoll_server,
             std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
             std::unique_ptr<ProofVerifier> proof_verifier);
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const ParsedQuicVersionVector& supported_versions,
             const QuicConfig& config,
             QuicEpollServer* epoll_server,
             std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
             std::unique_ptr<ProofVerifier> proof_verifier);
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const ParsedQuicVersionVector& supported_versions,
             const QuicConfig& config,
             QuicEpollServer* epoll_server,
             std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
             std::unique_ptr<ProofVerifier> proof_verifier,
             std::unique_ptr<SessionCache> session_cache);
  QuicClient(const QuicClient&) = delete;
  QuicClient& operator=(const QuicClient&) = delete;

  ~QuicClient() override;

  std::unique_ptr<QuicSession> CreateQuicClientSession(
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection) override;

  // Exposed for the quic client test.
  int GetLatestFD() const { return epoll_network_helper()->GetLatestFD(); }

  QuicClientEpollNetworkHelper* epoll_network_helper();
  const QuicClientEpollNetworkHelper* epoll_network_helper() const;

 private:
  friend class test::QuicClientPeer;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_CLIENT_H_
