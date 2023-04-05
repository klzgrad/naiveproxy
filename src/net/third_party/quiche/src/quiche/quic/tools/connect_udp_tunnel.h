// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_CONNECT_UDP_TUNNEL_H_
#define QUICHE_QUIC_TOOLS_CONNECT_UDP_TUNNEL_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/connecting_client_socket.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/socket_factory.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

// Manages a single UDP tunnel for a CONNECT-UDP proxy (see RFC 9298).
class ConnectUdpTunnel : public ConnectingClientSocket::AsyncVisitor,
                         public QuicSpdyStream::Http3DatagramVisitor {
 public:
  // `client_stream_request_handler` and `socket_factory` must both outlive the
  // created ConnectUdpTunnel. `server_label` is an identifier (typically
  // randomly generated) to indentify the server or backend in error headers,
  // per the requirements of RFC 9209, Section 2.
  ConnectUdpTunnel(
      QuicSimpleServerBackend::RequestHandler* client_stream_request_handler,
      SocketFactory* socket_factory, std::string server_label,
      absl::flat_hash_set<QuicServerId> acceptable_targets);
  ~ConnectUdpTunnel();
  ConnectUdpTunnel(const ConnectUdpTunnel&) = delete;
  ConnectUdpTunnel& operator=(const ConnectUdpTunnel&) = delete;

  // Attempts to open UDP tunnel to target server and then sends appropriate
  // success/error response to the request stream. `request_headers` must
  // represent headers from a CONNECT-UDP request, that is ":method"="CONNECT"
  // and ":protocol"="connect-udp".
  void OpenTunnel(const spdy::Http2HeaderBlock& request_headers);

  // Returns true iff the tunnel to the target server is currently open
  bool IsTunnelOpenToTarget() const;

  // Called when the client stream has been closed.  Tunnel to target
  // server is closed if open.  The RequestHandler will no longer be
  // interacted with after completion.
  void OnClientStreamClose();

  // ConnectingClientSocket::AsyncVisitor:
  void ConnectComplete(absl::Status status) override;
  void ReceiveComplete(absl::StatusOr<quiche::QuicheMemSlice> data) override;
  void SendComplete(absl::Status status) override;

  // QuicSpdyStream::Http3DatagramVisitor:
  void OnHttp3Datagram(QuicStreamId stream_id,
                       absl::string_view payload) override;

 private:
  void BeginAsyncReadFromTarget();
  void OnDataReceivedFromTarget(bool success);

  void SendUdpPacketToTarget(absl::string_view packet);

  void SendConnectResponse();
  void SendErrorResponse(absl::string_view status,
                         absl::string_view proxy_status_error,
                         absl::string_view error_details);
  void TerminateClientStream(absl::string_view error_description,
                             QuicResetStreamError error_code);

  const absl::flat_hash_set<QuicServerId> acceptable_targets_;
  SocketFactory* const socket_factory_;
  const std::string server_label_;

  // Null when client stream closed.
  QuicSimpleServerBackend::RequestHandler* client_stream_request_handler_;

  // Null when target connection disconnected.
  std::unique_ptr<ConnectingClientSocket> target_socket_;

  bool receive_started_ = false;
  bool datagram_visitor_registered_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_CONNECT_UDP_TUNNEL_H_
