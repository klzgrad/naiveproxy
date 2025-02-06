// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CONNECT_PROXY_CONNECT_SERVER_BACKEND_H_
#define QUICHE_QUIC_CONNECT_PROXY_CONNECT_SERVER_BACKEND_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/socket_factory.h"
#include "quiche/quic/tools/connect_tunnel.h"
#include "quiche/quic/tools/connect_udp_tunnel.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"

namespace quic {

// QUIC server backend that handles CONNECT and CONNECT-UDP requests.
// Non-CONNECT requests are delegated to a separate backend.
class ConnectServerBackend : public QuicSimpleServerBackend {
 public:
  // `server_label` is an identifier (typically randomly generated) to identify
  // the server or backend in error headers, per the requirements of RFC 9209,
  // Section 2.
  ConnectServerBackend(
      std::unique_ptr<QuicSimpleServerBackend> non_connect_backend,
      absl::flat_hash_set<QuicServerId> acceptable_connect_destinations,
      absl::flat_hash_set<QuicServerId> acceptable_connect_udp_targets,
      std::string server_label);

  ConnectServerBackend(const ConnectServerBackend&) = delete;
  ConnectServerBackend& operator=(const ConnectServerBackend&) = delete;

  ~ConnectServerBackend() override;

  // QuicSimpleServerBackend:
  bool InitializeBackend(const std::string& backend_url) override;
  bool IsBackendInitialized() const override;
  void SetSocketFactory(SocketFactory* socket_factory) override;
  void FetchResponseFromBackend(const quiche::HttpHeaderBlock& request_headers,
                                const std::string& request_body,
                                RequestHandler* request_handler) override;
  void HandleConnectHeaders(const quiche::HttpHeaderBlock& request_headers,
                            RequestHandler* request_handler) override;
  void HandleConnectData(absl::string_view data, bool data_complete,
                         RequestHandler* request_handler) override;
  void CloseBackendResponseStream(
      QuicSimpleServerBackend::RequestHandler* request_handler) override;

 private:
  std::unique_ptr<QuicSimpleServerBackend> non_connect_backend_;
  const absl::flat_hash_set<QuicServerId> acceptable_connect_destinations_;
  const absl::flat_hash_set<QuicServerId> acceptable_connect_udp_targets_;
  const std::string server_label_;

  SocketFactory* socket_factory_ = nullptr;  // unowned
  absl::flat_hash_map<std::pair<QuicConnectionId, QuicStreamId>,
                      std::unique_ptr<ConnectTunnel>>
      connect_tunnels_;
  absl::flat_hash_map<std::pair<QuicConnectionId, QuicStreamId>,
                      std::unique_ptr<ConnectUdpTunnel>>
      connect_udp_tunnels_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CONNECT_PROXY_CONNECT_SERVER_BACKEND_H_
