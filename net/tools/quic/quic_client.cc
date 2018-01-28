// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_client.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "base/run_loop.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/quic_connection.h"
#include "net/quic/core/quic_data_reader.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_server_id.h"
#include "net/quic/core/spdy_utils.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/tools/quic/platform/impl/quic_socket_utils.h"
#include "net/tools/quic/quic_epoll_alarm_factory.h"
#include "net/tools/quic/quic_epoll_connection_helper.h"

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

// TODO(rtenneti): Add support for MMSG_MORE.
#define MMSG_MORE 0
using std::string;

namespace net {

QuicClient::QuicClient(QuicSocketAddress server_address,
                       const QuicServerId& server_id,
                       const QuicTransportVersionVector& supported_versions,
                       EpollServer* epoll_server,
                       std::unique_ptr<ProofVerifier> proof_verifier)
    : QuicClient(
          server_address,
          server_id,
          supported_versions,
          QuicConfig(),
          epoll_server,
          QuicWrapUnique(new QuicClientEpollNetworkHelper(epoll_server, this)),
          std::move(proof_verifier)) {}

QuicClient::QuicClient(
    QuicSocketAddress server_address,
    const QuicServerId& server_id,
    const QuicTransportVersionVector& supported_versions,
    EpollServer* epoll_server,
    std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier)
    : QuicClient(server_address,
                 server_id,
                 supported_versions,
                 QuicConfig(),
                 epoll_server,
                 std::move(network_helper),
                 std::move(proof_verifier)) {}

QuicClient::QuicClient(
    QuicSocketAddress server_address,
    const QuicServerId& server_id,
    const QuicTransportVersionVector& supported_versions,
    const QuicConfig& config,
    EpollServer* epoll_server,
    std::unique_ptr<QuicClientEpollNetworkHelper> network_helper,
    std::unique_ptr<ProofVerifier> proof_verifier)
    : QuicSpdyClientBase(
          server_id,
          supported_versions,
          config,
          new QuicEpollConnectionHelper(epoll_server, QuicAllocator::SIMPLE),
          new QuicEpollAlarmFactory(epoll_server),
          std::move(network_helper),
          std::move(proof_verifier)) {
  set_server_address(server_address);
}

QuicClient::~QuicClient() {}

QuicClientEpollNetworkHelper* QuicClient::epoll_network_helper() {
  return static_cast<QuicClientEpollNetworkHelper*>(network_helper());
}

const QuicClientEpollNetworkHelper* QuicClient::epoll_network_helper() const {
  return static_cast<const QuicClientEpollNetworkHelper*>(network_helper());
}

}  // namespace net
