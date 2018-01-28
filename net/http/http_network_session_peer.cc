// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_network_session_peer.h"

#include "net/base/network_throttle_manager.h"
#include "net/http/http_network_session.h"
#include "net/http/http_proxy_client_socket_pool.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/socks_client_socket_pool.h"
#include "net/socket/ssl_client_socket_pool.h"
#include "net/socket/transport_client_socket_pool.h"

namespace net {

HttpNetworkSessionPeer::HttpNetworkSessionPeer(HttpNetworkSession* session)
    : session_(session) {}

HttpNetworkSessionPeer::~HttpNetworkSessionPeer() {}

void HttpNetworkSessionPeer::SetClientSocketPoolManager(
    std::unique_ptr<ClientSocketPoolManager> socket_pool_manager) {
  session_->normal_socket_pool_manager_.swap(socket_pool_manager);
}

void HttpNetworkSessionPeer::SetHttpStreamFactory(
    std::unique_ptr<HttpStreamFactory> http_stream_factory) {
  session_->http_stream_factory_.swap(http_stream_factory);
}

void HttpNetworkSessionPeer::SetHttpStreamFactoryForWebSocket(
    std::unique_ptr<HttpStreamFactory> http_stream_factory) {
  session_->http_stream_factory_for_websocket_.swap(http_stream_factory);
}

void HttpNetworkSessionPeer::SetNetworkStreamThrottler(
    std::unique_ptr<NetworkThrottleManager> network_throttle_manager) {
  session_->network_stream_throttler_.swap(network_throttle_manager);
}

}  // namespace net
