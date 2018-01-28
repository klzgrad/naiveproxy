// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A toy client, which connects to a specified port and sends QUIC
// request to that endpoint.

#ifndef NET_TOOLS_QUIC_QUIC_CLIENT_H_
#define NET_TOOLS_QUIC_QUIC_CLIENT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/macros.h"
#include "net/quic/core/quic_client_push_promise_index.h"
#include "net/quic/core/quic_config.h"
#include "net/quic/core/quic_spdy_stream.h"
#include "net/quic/platform/api/quic_containers.h"
#include "net/tools/epoll_server/epoll_server.h"
#include "net/tools/quic/quic_client_base.h"
#include "net/tools/quic/quic_client_epoll_network_helper.h"
#include "net/tools/quic/quic_packet_reader.h"
#include "net/tools/quic/quic_process_packet_interface.h"
#include "net/tools/quic/quic_spdy_client_base.h"
#include "net/tools/quic/quic_spdy_client_session.h"

namespace net {

class QuicServerId;

namespace test {
class QuicClientPeer;
}  // namespace test

class QuicClient : public QuicSpdyClientBase {
 public:
  // This will create its own QuicClientEpollNetworkHelper.
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const QuicTransportVersionVector& supported_versions,
             EpollServer* epoll_server,
             std::unique_ptr<ProofVerifier> proof_verifier);
  // This will take ownership of a passed in network primitive.
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const QuicTransportVersionVector& supported_versions,
             EpollServer* epoll_server,
             std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
             std::unique_ptr<ProofVerifier> proof_verifier);
  QuicClient(QuicSocketAddress server_address,
             const QuicServerId& server_id,
             const QuicTransportVersionVector& supported_versions,
             const QuicConfig& config,
             EpollServer* epoll_server,
             std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
             std::unique_ptr<ProofVerifier> proof_verifier);

  ~QuicClient() override;

  // Exposed for the quic client test.
  int GetLatestFD() const { return epoll_network_helper()->GetLatestFD(); }

  QuicClientEpollNetworkHelper* epoll_network_helper();
  const QuicClientEpollNetworkHelper* epoll_network_helper() const;

 private:
  friend class test::QuicClientPeer;

  DISALLOW_COPY_AND_ASSIGN(QuicClient);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_CLIENT_H_
