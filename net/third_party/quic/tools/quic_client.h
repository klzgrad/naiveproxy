// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A toy client, which connects to a specified port and sends QUIC
// request to that endpoint.

#ifndef NET_THIRD_PARTY_QUIC_TOOLS_QUIC_CLIENT_H_
#define NET_THIRD_PARTY_QUIC_TOOLS_QUIC_CLIENT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/macros.h"
#include "net/third_party/quic/core/http/quic_client_push_promise_index.h"
#include "net/third_party/quic/core/http/quic_spdy_client_session.h"
#include "net/third_party/quic/core/http/quic_spdy_stream.h"
#include "net/third_party/quic/core/quic_config.h"
#include "net/third_party/quic/core/quic_packet_reader.h"
#include "net/third_party/quic/core/quic_process_packet_interface.h"
#include "net/third_party/quic/platform/api/quic_containers.h"
#include "net/third_party/quic/tools/quic_client_base.h"
#include "net/third_party/quic/tools/quic_client_epoll_network_helper.h"
#include "net/third_party/quic/tools/quic_spdy_client_base.h"
#include "net/tools/epoll_server/epoll_server.h"

namespace quic {

class QuicServerId;

namespace test {
class QuicClientPeer;
}  // namespace test

class QuicClient : public QuicSpdyClientBase {
 public:
  // This will create its own QuicClientEpollNetworkHelper.
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const ParsedQuicVersionVector& supported_versions,
             net::EpollServer* epoll_server,
             std::unique_ptr<ProofVerifier> proof_verifier);
  // This will take ownership of a passed in network primitive.
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const ParsedQuicVersionVector& supported_versions,
             net::EpollServer* epoll_server,
             std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
             std::unique_ptr<ProofVerifier> proof_verifier);
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const ParsedQuicVersionVector& supported_versions,
             const QuicConfig& config,
             net::EpollServer* epoll_server,
             std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
             std::unique_ptr<ProofVerifier> proof_verifier);
  QuicClient(const QuicClient&) = delete;
  QuicClient& operator=(const QuicClient&) = delete;

  ~QuicClient() override;

  std::unique_ptr<QuicSession> CreateQuicClientSession(
      QuicConnection* connection) override;

  // Exposed for the quic client test.
  int GetLatestFD() const { return epoll_network_helper()->GetLatestFD(); }

  QuicClientEpollNetworkHelper* epoll_network_helper();
  const QuicClientEpollNetworkHelper* epoll_network_helper() const;

  void set_drop_response_body(bool drop_response_body) {
    drop_response_body_ = drop_response_body;
  }

 private:
  friend class test::QuicClientPeer;
  bool drop_response_body_ = false;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TOOLS_QUIC_CLIENT_H_
