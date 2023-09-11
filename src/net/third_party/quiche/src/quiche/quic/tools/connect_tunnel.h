// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_CONNECT_TUNNEL_H_
#define QUICHE_QUIC_TOOLS_CONNECT_TUNNEL_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/connecting_client_socket.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/socket_factory.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

// Manages a single connection tunneled over a CONNECT proxy.
class ConnectTunnel : public ConnectingClientSocket::AsyncVisitor {
 public:
  // `client_stream_request_handler` and `socket_factory` must both outlive the
  // created ConnectTunnel.
  ConnectTunnel(
      QuicSimpleServerBackend::RequestHandler* client_stream_request_handler,
      SocketFactory* socket_factory,
      absl::flat_hash_set<QuicServerId> acceptable_destinations);
  ~ConnectTunnel();
  ConnectTunnel(const ConnectTunnel&) = delete;
  ConnectTunnel& operator=(const ConnectTunnel&) = delete;

  // Attempts to open TCP connection to destination server and then sends
  // appropriate success/error response to the request stream. `request_headers`
  // must represent headers from a CONNECT request, that is ":method"="CONNECT"
  // and no ":protocol".
  void OpenTunnel(const spdy::Http2HeaderBlock& request_headers);

  // Returns true iff the connection to the destination server is currently open
  bool IsConnectedToDestination() const;

  void SendDataToDestination(absl::string_view data);

  // Called when the client stream has been closed.  Connection to destination
  // server is closed if connected.  The RequestHandler will no longer be
  // interacted with after completion.
  void OnClientStreamClose();

  // ConnectingClientSocket::AsyncVisitor:
  void ConnectComplete(absl::Status status) override;
  void ReceiveComplete(absl::StatusOr<quiche::QuicheMemSlice> data) override;
  void SendComplete(absl::Status status) override;

 private:
  void BeginAsyncReadFromDestination();
  void OnDataReceivedFromDestination(bool success);

  // For normal (FIN) closure. Errors (RST) should result in directly calling
  // TerminateClientStream().
  void OnDestinationConnectionClosed();

  void SendConnectResponse();
  void TerminateClientStream(
      absl::string_view error_description,
      QuicResetStreamError error_code =
          QuicResetStreamError::FromIetf(QuicHttp3ErrorCode::CONNECT_ERROR));

  const absl::flat_hash_set<QuicServerId> acceptable_destinations_;
  SocketFactory* const socket_factory_;

  // Null when client stream closed.
  QuicSimpleServerBackend::RequestHandler* client_stream_request_handler_;

  // Null when destination connection disconnected.
  std::unique_ptr<ConnectingClientSocket> destination_socket_;

  bool receive_started_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_CONNECT_TUNNEL_H_
