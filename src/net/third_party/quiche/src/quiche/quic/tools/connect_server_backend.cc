// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/connect_server_backend.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/socket_factory.h"
#include "quiche/quic/tools/connect_tunnel.h"
#include "quiche/quic/tools/connect_udp_tunnel.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

namespace {

void SendErrorResponse(QuicSimpleServerBackend::RequestHandler* request_handler,
                       absl::string_view error_code) {
  quiche::HttpHeaderBlock headers;
  headers[":status"] = error_code;
  QuicBackendResponse response;
  response.set_headers(std::move(headers));
  request_handler->OnResponseBackendComplete(&response);
}

}  // namespace

ConnectServerBackend::ConnectServerBackend(
    std::unique_ptr<QuicSimpleServerBackend> non_connect_backend,
    absl::flat_hash_set<QuicServerId> acceptable_connect_destinations,
    absl::flat_hash_set<QuicServerId> acceptable_connect_udp_targets,
    std::string server_label)
    : non_connect_backend_(std::move(non_connect_backend)),
      acceptable_connect_destinations_(
          std::move(acceptable_connect_destinations)),
      acceptable_connect_udp_targets_(
          std::move(acceptable_connect_udp_targets)),
      server_label_(std::move(server_label)) {
  QUICHE_DCHECK(non_connect_backend_);
  QUICHE_DCHECK(!server_label_.empty());
}

ConnectServerBackend::~ConnectServerBackend() {
  // Expect all streams to be closed before destroying backend.
  QUICHE_DCHECK(connect_tunnels_.empty());
  QUICHE_DCHECK(connect_udp_tunnels_.empty());
}

bool ConnectServerBackend::InitializeBackend(const std::string&) {
  return true;
}

bool ConnectServerBackend::IsBackendInitialized() const { return true; }

void ConnectServerBackend::SetSocketFactory(SocketFactory* socket_factory) {
  QUICHE_DCHECK(socket_factory);
  QUICHE_DCHECK(connect_tunnels_.empty());
  QUICHE_DCHECK(connect_udp_tunnels_.empty());
  socket_factory_ = socket_factory;
}

void ConnectServerBackend::FetchResponseFromBackend(
    const quiche::HttpHeaderBlock& request_headers,
    const std::string& request_body, RequestHandler* request_handler) {
  // Not a CONNECT request, so send to `non_connect_backend_`.
  non_connect_backend_->FetchResponseFromBackend(request_headers, request_body,
                                                 request_handler);
}

void ConnectServerBackend::HandleConnectHeaders(
    const quiche::HttpHeaderBlock& request_headers,
    RequestHandler* request_handler) {
  QUICHE_DCHECK(request_headers.contains(":method") &&
                request_headers.find(":method")->second == "CONNECT");

  if (!socket_factory_) {
    QUICHE_BUG(connect_server_backend_no_socket_factory)
        << "Must set socket factory before ConnectServerBackend receives "
           "requests.";
    SendErrorResponse(request_handler, "500");
    return;
  }

  if (!request_headers.contains(":protocol")) {
    // normal CONNECT
    auto [tunnel_it, inserted] = connect_tunnels_.emplace(
        std::make_pair(request_handler->connection_id(),
                       request_handler->stream_id()),
        std::make_unique<ConnectTunnel>(request_handler, socket_factory_,
                                        acceptable_connect_destinations_));
    QUICHE_DCHECK(inserted);

    tunnel_it->second->OpenTunnel(request_headers);
  } else if (request_headers.find(":protocol")->second == "connect-udp") {
    // CONNECT-UDP
    auto [tunnel_it, inserted] = connect_udp_tunnels_.emplace(
        std::make_pair(request_handler->connection_id(),
                       request_handler->stream_id()),
        std::make_unique<ConnectUdpTunnel>(request_handler, socket_factory_,
                                           server_label_,
                                           acceptable_connect_udp_targets_));
    QUICHE_DCHECK(inserted);

    tunnel_it->second->OpenTunnel(request_headers);
  } else {
    // Not a supported request.
    non_connect_backend_->HandleConnectHeaders(request_headers,
                                               request_handler);
  }
}

void ConnectServerBackend::HandleConnectData(absl::string_view data,
                                             bool data_complete,
                                             RequestHandler* request_handler) {
  // Expect ConnectUdpTunnels to register a datagram visitor, causing the
  // stream to process data as capsules.  HandleConnectData() should therefore
  // never be called for streams with a ConnectUdpTunnel.
  QUICHE_DCHECK(!connect_udp_tunnels_.contains(std::make_pair(
      request_handler->connection_id(), request_handler->stream_id())));

  auto tunnel_it = connect_tunnels_.find(std::make_pair(
      request_handler->connection_id(), request_handler->stream_id()));
  if (tunnel_it == connect_tunnels_.end()) {
    // If tunnel not found, perhaps it's something being handled for
    // non-CONNECT. Possible because this method could be called for anything
    // with a ":method":"CONNECT" header, but this class does not handle such
    // requests if they have a ":protocol" header.
    non_connect_backend_->HandleConnectData(data, data_complete,
                                            request_handler);
    return;
  }

  if (!data.empty()) {
    tunnel_it->second->SendDataToDestination(data);
  }
  if (data_complete) {
    tunnel_it->second->OnClientStreamClose();
    connect_tunnels_.erase(tunnel_it);
  }
}

void ConnectServerBackend::CloseBackendResponseStream(
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  auto tunnel_it = connect_tunnels_.find(std::make_pair(
      request_handler->connection_id(), request_handler->stream_id()));
  if (tunnel_it != connect_tunnels_.end()) {
    tunnel_it->second->OnClientStreamClose();
    connect_tunnels_.erase(tunnel_it);
  }

  auto udp_tunnel_it = connect_udp_tunnels_.find(std::pair(
      request_handler->connection_id(), request_handler->stream_id()));
  if (udp_tunnel_it != connect_udp_tunnels_.end()) {
    udp_tunnel_it->second->OnClientStreamClose();
    connect_udp_tunnels_.erase(udp_tunnel_it);
  }

  non_connect_backend_->CloseBackendResponseStream(request_handler);
}

}  // namespace quic
