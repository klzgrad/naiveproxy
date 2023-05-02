// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/connect_tunnel.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/socket_factory.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_backend_response.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

namespace {

// Arbitrarily chosen. No effort has been made to figure out an optimal size.
constexpr size_t kReadSize = 4 * 1024;

absl::optional<QuicServerId> ValidateHeadersAndGetAuthority(
    const spdy::Http2HeaderBlock& request_headers) {
  QUICHE_DCHECK(request_headers.contains(":method"));
  QUICHE_DCHECK(request_headers.find(":method")->second == "CONNECT");
  QUICHE_DCHECK(!request_headers.contains(":protocol"));

  auto scheme_it = request_headers.find(":scheme");
  if (scheme_it != request_headers.end()) {
    QUICHE_DVLOG(1) << "CONNECT request contains unexpected scheme: "
                    << scheme_it->second;
    return absl::nullopt;
  }

  auto path_it = request_headers.find(":path");
  if (path_it != request_headers.end()) {
    QUICHE_DVLOG(1) << "CONNECT request contains unexpected path: "
                    << path_it->second;
    return absl::nullopt;
  }

  auto authority_it = request_headers.find(":authority");
  if (authority_it == request_headers.end() || authority_it->second.empty()) {
    QUICHE_DVLOG(1) << "CONNECT request missing authority";
    return absl::nullopt;
  }

  // A valid CONNECT authority must contain host and port and nothing else, per
  // https://www.rfc-editor.org/rfc/rfc9110.html#name-connect. This matches the
  // host and port parsing rules for QuicServerId.
  absl::optional<QuicServerId> server_id =
      QuicServerId::ParseFromHostPortString(authority_it->second);
  if (!server_id.has_value()) {
    QUICHE_DVLOG(1) << "CONNECT request authority is malformed: "
                    << authority_it->second;
    return absl::nullopt;
  }

  return server_id;
}

bool ValidateAuthority(
    const QuicServerId& authority,
    const absl::flat_hash_set<QuicServerId>& acceptable_destinations) {
  if (acceptable_destinations.contains(authority)) {
    return true;
  }

  QUICHE_DVLOG(1) << "CONNECT request authority: "
                  << authority.ToHostPortString()
                  << " is not an acceptable allow-listed destiation ";
  return false;
}

}  // namespace

ConnectTunnel::ConnectTunnel(
    QuicSimpleServerBackend::RequestHandler* client_stream_request_handler,
    SocketFactory* socket_factory,
    absl::flat_hash_set<QuicServerId> acceptable_destinations)
    : acceptable_destinations_(std::move(acceptable_destinations)),
      socket_factory_(socket_factory),
      client_stream_request_handler_(client_stream_request_handler) {
  QUICHE_DCHECK(client_stream_request_handler_);
  QUICHE_DCHECK(socket_factory_);
}

ConnectTunnel::~ConnectTunnel() {
  // Expect client and destination sides of tunnel to both be closed before
  // destruction.
  QUICHE_DCHECK_EQ(client_stream_request_handler_, nullptr);
  QUICHE_DCHECK(!IsConnectedToDestination());
  QUICHE_DCHECK(!receive_started_);
}

void ConnectTunnel::OpenTunnel(const spdy::Http2HeaderBlock& request_headers) {
  QUICHE_DCHECK(!IsConnectedToDestination());

  absl::optional<QuicServerId> authority =
      ValidateHeadersAndGetAuthority(request_headers);
  if (!authority.has_value()) {
    TerminateClientStream(
        "invalid request headers",
        QuicResetStreamError::FromIetf(QuicHttp3ErrorCode::MESSAGE_ERROR));
    return;
  }

  if (!ValidateAuthority(authority.value(), acceptable_destinations_)) {
    TerminateClientStream(
        "disallowed request authority",
        QuicResetStreamError::FromIetf(QuicHttp3ErrorCode::REQUEST_REJECTED));
    return;
  }

  QuicSocketAddress address =
      tools::LookupAddress(AF_UNSPEC, authority.value());
  if (!address.IsInitialized()) {
    TerminateClientStream("host resolution error");
    return;
  }

  destination_socket_ =
      socket_factory_->CreateTcpClientSocket(address,
                                             /*receive_buffer_size=*/0,
                                             /*send_buffer_size=*/0,
                                             /*async_visitor=*/this);
  QUICHE_DCHECK(destination_socket_);

  absl::Status connect_result = destination_socket_->ConnectBlocking();
  if (!connect_result.ok()) {
    TerminateClientStream(
        "error connecting TCP socket to destination server: " +
        connect_result.ToString());
    return;
  }

  QUICHE_DVLOG(1) << "CONNECT tunnel opened from stream "
                  << client_stream_request_handler_->stream_id() << " to "
                  << authority.value().ToHostPortString();

  SendConnectResponse();
  BeginAsyncReadFromDestination();
}

bool ConnectTunnel::IsConnectedToDestination() const {
  return !!destination_socket_;
}

void ConnectTunnel::SendDataToDestination(absl::string_view data) {
  QUICHE_DCHECK(IsConnectedToDestination());
  QUICHE_DCHECK(!data.empty());

  absl::Status send_result =
      destination_socket_->SendBlocking(std::string(data));
  if (!send_result.ok()) {
    TerminateClientStream("TCP error sending data to destination server: " +
                          send_result.ToString());
  }
}

void ConnectTunnel::OnClientStreamClose() {
  QUICHE_DCHECK(client_stream_request_handler_);

  QUICHE_DVLOG(1) << "CONNECT stream "
                  << client_stream_request_handler_->stream_id() << " closed";

  client_stream_request_handler_ = nullptr;

  if (IsConnectedToDestination()) {
    // TODO(ericorth): Consider just calling shutdown() on the socket rather
    // than fully disconnecting in order to allow a graceful TCP FIN stream
    // shutdown per
    // https://www.rfc-editor.org/rfc/rfc9114.html#name-the-connect-method.
    // Would require shutdown support in the socket library, and would need to
    // deal with the tunnel/socket outliving the client stream.
    destination_socket_->Disconnect();
  }

  // Clear socket pointer.
  destination_socket_.reset();
}

void ConnectTunnel::ConnectComplete(absl::Status /*status*/) {
  // Async connect not expected.
  QUICHE_NOTREACHED();
}

void ConnectTunnel::ReceiveComplete(
    absl::StatusOr<quiche::QuicheMemSlice> data) {
  QUICHE_DCHECK(IsConnectedToDestination());
  QUICHE_DCHECK(receive_started_);

  receive_started_ = false;

  if (!data.ok()) {
    if (client_stream_request_handler_) {
      TerminateClientStream("TCP error receiving data from destination server");
    } else {
      // This typically just means a receive operation was cancelled on calling
      // destination_socket_->Disconnect().
      QUICHE_DVLOG(1) << "TCP error receiving data from destination server "
                         "after stream already closed.";
    }
    return;
  } else if (data.value().empty()) {
    OnDestinationConnectionClosed();
    return;
  }

  QUICHE_DCHECK(client_stream_request_handler_);
  client_stream_request_handler_->SendStreamData(data.value().AsStringView(),
                                                 /*close_stream=*/false);

  BeginAsyncReadFromDestination();
}

void ConnectTunnel::SendComplete(absl::Status /*status*/) {
  // Async send not expected.
  QUICHE_NOTREACHED();
}

void ConnectTunnel::BeginAsyncReadFromDestination() {
  QUICHE_DCHECK(IsConnectedToDestination());
  QUICHE_DCHECK(client_stream_request_handler_);
  QUICHE_DCHECK(!receive_started_);

  receive_started_ = true;
  destination_socket_->ReceiveAsync(kReadSize);
}

void ConnectTunnel::OnDestinationConnectionClosed() {
  QUICHE_DCHECK(IsConnectedToDestination());
  QUICHE_DCHECK(client_stream_request_handler_);

  QUICHE_DVLOG(1) << "CONNECT stream "
                  << client_stream_request_handler_->stream_id()
                  << " destination connection closed";
  destination_socket_->Disconnect();

  // Clear socket pointer.
  destination_socket_.reset();

  // Extra check that nothing in the Disconnect could lead to terminating the
  // stream.
  QUICHE_DCHECK(client_stream_request_handler_);

  client_stream_request_handler_->SendStreamData("", /*close_stream=*/true);
}

void ConnectTunnel::SendConnectResponse() {
  QUICHE_DCHECK(IsConnectedToDestination());
  QUICHE_DCHECK(client_stream_request_handler_);

  spdy::Http2HeaderBlock response_headers;
  response_headers[":status"] = "200";

  QuicBackendResponse response;
  response.set_headers(std::move(response_headers));
  // Need to leave the stream open after sending the CONNECT response.
  response.set_response_type(QuicBackendResponse::INCOMPLETE_RESPONSE);

  client_stream_request_handler_->OnResponseBackendComplete(&response);
}

void ConnectTunnel::TerminateClientStream(absl::string_view error_description,
                                          QuicResetStreamError error_code) {
  QUICHE_DCHECK(client_stream_request_handler_);

  std::string error_description_str =
      error_description.empty() ? ""
                                : absl::StrCat(" due to ", error_description);
  QUICHE_DVLOG(1) << "Terminating CONNECT stream "
                  << client_stream_request_handler_->stream_id()
                  << " with error code " << error_code.ietf_application_code()
                  << error_description_str;

  client_stream_request_handler_->TerminateStreamWithError(error_code);
}

}  // namespace quic
