// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/connect_udp_tunnel.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/socket_factory.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_backend_response.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/common/masque/connect_udp_datagram_payload.h"
#include "quiche/common/platform/api/quiche_googleurl.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/platform/api/quiche_url_utils.h"
#include "quiche/common/structured_headers.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

namespace structured_headers = quiche::structured_headers;

namespace {

// Arbitrarily chosen. No effort has been made to figure out an optimal size.
constexpr size_t kReadSize = 4 * 1024;

// Only support the default path
// ("/.well-known/masque/udp/{target_host}/{target_port}/")
std::optional<QuicServerId> ValidateAndParseTargetFromPath(
    absl::string_view path) {
  std::string canonicalized_path_str;
  url::StdStringCanonOutput canon_output(&canonicalized_path_str);
  url::Component path_component;
  url::CanonicalizePath(path.data(), url::Component(0, path.size()),
                        &canon_output, &path_component);
  if (!path_component.is_nonempty()) {
    QUICHE_DVLOG(1) << "CONNECT-UDP request with non-canonicalizable path: "
                    << path;
    return std::nullopt;
  }
  canon_output.Complete();
  absl::string_view canonicalized_path =
      absl::string_view(canonicalized_path_str)
          .substr(path_component.begin, path_component.len);

  std::vector<absl::string_view> path_split =
      absl::StrSplit(canonicalized_path, '/');
  if (path_split.size() != 7 || !path_split[0].empty() ||
      path_split[1] != ".well-known" || path_split[2] != "masque" ||
      path_split[3] != "udp" || path_split[4].empty() ||
      path_split[5].empty() || !path_split[6].empty()) {
    QUICHE_DVLOG(1) << "CONNECT-UDP request with bad path: "
                    << canonicalized_path;
    return std::nullopt;
  }

  std::optional<std::string> decoded_host =
      quiche::AsciiUrlDecode(path_split[4]);
  if (!decoded_host.has_value()) {
    QUICHE_DVLOG(1) << "CONNECT-UDP request with undecodable host: "
                    << path_split[4];
    return std::nullopt;
  }
  // Empty host checked above after path split. Expect decoding to never result
  // in an empty decoded host from non-empty encoded host.
  QUICHE_DCHECK(!decoded_host->empty());

  std::optional<std::string> decoded_port =
      quiche::AsciiUrlDecode(path_split[5]);
  if (!decoded_port.has_value()) {
    QUICHE_DVLOG(1) << "CONNECT-UDP request with undecodable port: "
                    << path_split[5];
    return std::nullopt;
  }
  // Empty port checked above after path split. Expect decoding to never result
  // in an empty decoded port from non-empty encoded port.
  QUICHE_DCHECK(!decoded_port->empty());

  int parsed_port_number = url::ParsePort(
      decoded_port->data(), url::Component(0, decoded_port->size()));
  // Negative result is either invalid or unspecified, either of which is
  // disallowed for this parse. Port 0 is technically valid but reserved and not
  // really usable in practice, so easiest to just disallow it here.
  if (parsed_port_number <= 0) {
    QUICHE_DVLOG(1) << "CONNECT-UDP request with bad port: " << *decoded_port;
    return std::nullopt;
  }
  // Expect url::ParsePort() to validate port is uint16_t and otherwise return
  // negative number checked for above.
  QUICHE_DCHECK_LE(parsed_port_number, std::numeric_limits<uint16_t>::max());

  return QuicServerId(*decoded_host, static_cast<uint16_t>(parsed_port_number));
}

// Validate header expectations from RFC 9298, section 3.4.
std::optional<QuicServerId> ValidateHeadersAndGetTarget(
    const spdy::Http2HeaderBlock& request_headers) {
  QUICHE_DCHECK(request_headers.contains(":method"));
  QUICHE_DCHECK(request_headers.find(":method")->second == "CONNECT");
  QUICHE_DCHECK(request_headers.contains(":protocol"));
  QUICHE_DCHECK(request_headers.find(":protocol")->second == "connect-udp");

  auto authority_it = request_headers.find(":authority");
  if (authority_it == request_headers.end() || authority_it->second.empty()) {
    QUICHE_DVLOG(1) << "CONNECT-UDP request missing authority";
    return std::nullopt;
  }
  // For toy server simplicity, skip validating that the authority matches the
  // current server.

  auto scheme_it = request_headers.find(":scheme");
  if (scheme_it == request_headers.end() || scheme_it->second.empty()) {
    QUICHE_DVLOG(1) << "CONNECT-UDP request missing scheme";
    return std::nullopt;
  } else if (scheme_it->second != "https") {
    QUICHE_DVLOG(1) << "CONNECT-UDP request contains unexpected scheme: "
                    << scheme_it->second;
    return std::nullopt;
  }

  auto path_it = request_headers.find(":path");
  if (path_it == request_headers.end() || path_it->second.empty()) {
    QUICHE_DVLOG(1) << "CONNECT-UDP request missing path";
    return std::nullopt;
  }
  std::optional<QuicServerId> target_server_id =
      ValidateAndParseTargetFromPath(path_it->second);

  return target_server_id;
}

bool ValidateTarget(
    const QuicServerId& target,
    const absl::flat_hash_set<QuicServerId>& acceptable_targets) {
  if (acceptable_targets.contains(target)) {
    return true;
  }

  QUICHE_DVLOG(1)
      << "CONNECT-UDP request target is not an acceptable allow-listed target: "
      << target.ToHostPortString();
  return false;
}

}  // namespace

ConnectUdpTunnel::ConnectUdpTunnel(
    QuicSimpleServerBackend::RequestHandler* client_stream_request_handler,
    SocketFactory* socket_factory, std::string server_label,
    absl::flat_hash_set<QuicServerId> acceptable_targets)
    : acceptable_targets_(std::move(acceptable_targets)),
      socket_factory_(socket_factory),
      server_label_(std::move(server_label)),
      client_stream_request_handler_(client_stream_request_handler) {
  QUICHE_DCHECK(client_stream_request_handler_);
  QUICHE_DCHECK(socket_factory_);
  QUICHE_DCHECK(!server_label_.empty());
}

ConnectUdpTunnel::~ConnectUdpTunnel() {
  // Expect client and target sides of tunnel to both be closed before
  // destruction.
  QUICHE_DCHECK(!IsTunnelOpenToTarget());
  QUICHE_DCHECK(!receive_started_);
  QUICHE_DCHECK(!datagram_visitor_registered_);
}

void ConnectUdpTunnel::OpenTunnel(
    const spdy::Http2HeaderBlock& request_headers) {
  QUICHE_DCHECK(!IsTunnelOpenToTarget());

  std::optional<QuicServerId> target =
      ValidateHeadersAndGetTarget(request_headers);
  if (!target.has_value()) {
    // Malformed request.
    TerminateClientStream(
        "invalid request headers",
        QuicResetStreamError::FromIetf(QuicHttp3ErrorCode::MESSAGE_ERROR));
    return;
  }

  if (!ValidateTarget(*target, acceptable_targets_)) {
    SendErrorResponse("403", "destination_ip_prohibited",
                      "disallowed proxy target");
    return;
  }

  // TODO(ericorth): Validate that the IP address doesn't fall into diallowed
  // ranges per RFC 9298, Section 7.
  QuicSocketAddress address = tools::LookupAddress(AF_UNSPEC, *target);
  if (!address.IsInitialized()) {
    SendErrorResponse("500", "dns_error", "host resolution error");
    return;
  }

  target_socket_ = socket_factory_->CreateConnectingUdpClientSocket(
      address,
      /*receive_buffer_size=*/0,
      /*send_buffer_size=*/0,
      /*async_visitor=*/this);
  QUICHE_DCHECK(target_socket_);

  absl::Status connect_result = target_socket_->ConnectBlocking();
  if (!connect_result.ok()) {
    SendErrorResponse(
        "502", "destination_ip_unroutable",
        absl::StrCat("UDP socket error: ", connect_result.ToString()));
    return;
  }

  QUICHE_DVLOG(1) << "CONNECT-UDP tunnel opened from stream "
                  << client_stream_request_handler_->stream_id() << " to "
                  << target->ToHostPortString();

  client_stream_request_handler_->GetStream()->RegisterHttp3DatagramVisitor(
      this);
  datagram_visitor_registered_ = true;

  SendConnectResponse();
  BeginAsyncReadFromTarget();
}

bool ConnectUdpTunnel::IsTunnelOpenToTarget() const { return !!target_socket_; }

void ConnectUdpTunnel::OnClientStreamClose() {
  QUICHE_CHECK(client_stream_request_handler_);

  QUICHE_DVLOG(1) << "CONNECT-UDP stream "
                  << client_stream_request_handler_->stream_id() << " closed";

  if (datagram_visitor_registered_) {
    client_stream_request_handler_->GetStream()
        ->UnregisterHttp3DatagramVisitor();
    datagram_visitor_registered_ = false;
  }
  client_stream_request_handler_ = nullptr;

  if (IsTunnelOpenToTarget()) {
    target_socket_->Disconnect();
  }

  // Clear socket pointer.
  target_socket_.reset();
}

void ConnectUdpTunnel::ConnectComplete(absl::Status /*status*/) {
  // Async connect not expected.
  QUICHE_NOTREACHED();
}

void ConnectUdpTunnel::ReceiveComplete(
    absl::StatusOr<quiche::QuicheMemSlice> data) {
  QUICHE_DCHECK(IsTunnelOpenToTarget());
  QUICHE_DCHECK(receive_started_);

  receive_started_ = false;

  if (!data.ok()) {
    if (client_stream_request_handler_) {
      QUICHE_LOG(WARNING) << "Error receiving CONNECT-UDP data from target: "
                          << data.status();
    } else {
      // This typically just means a receive operation was cancelled on calling
      // target_socket_->Disconnect().
      QUICHE_DVLOG(1) << "Error receiving CONNECT-UDP data from target after "
                         "stream already closed.";
    }
    return;
  }

  QUICHE_DCHECK(client_stream_request_handler_);
  quiche::ConnectUdpDatagramUdpPacketPayload payload(data->AsStringView());
  client_stream_request_handler_->GetStream()->SendHttp3Datagram(
      payload.Serialize());

  BeginAsyncReadFromTarget();
}

void ConnectUdpTunnel::SendComplete(absl::Status /*status*/) {
  // Async send not expected.
  QUICHE_NOTREACHED();
}

void ConnectUdpTunnel::OnHttp3Datagram(QuicStreamId stream_id,
                                       absl::string_view payload) {
  QUICHE_DCHECK(IsTunnelOpenToTarget());
  QUICHE_DCHECK_EQ(stream_id, client_stream_request_handler_->stream_id());
  QUICHE_DCHECK(!payload.empty());

  std::unique_ptr<quiche::ConnectUdpDatagramPayload> parsed_payload =
      quiche::ConnectUdpDatagramPayload::Parse(payload);
  if (!parsed_payload) {
    QUICHE_DVLOG(1) << "Ignoring HTTP Datagram payload, due to inability to "
                       "parse as CONNECT-UDP payload.";
    return;
  }

  switch (parsed_payload->GetType()) {
    case quiche::ConnectUdpDatagramPayload::Type::kUdpPacket:
      SendUdpPacketToTarget(parsed_payload->GetUdpProxyingPayload());
      break;
    case quiche::ConnectUdpDatagramPayload::Type::kUnknown:
      QUICHE_DVLOG(1)
          << "Ignoring HTTP Datagram payload with unrecognized context ID.";
  }
}

void ConnectUdpTunnel::BeginAsyncReadFromTarget() {
  QUICHE_DCHECK(IsTunnelOpenToTarget());
  QUICHE_DCHECK(client_stream_request_handler_);
  QUICHE_DCHECK(!receive_started_);

  receive_started_ = true;
  target_socket_->ReceiveAsync(kReadSize);
}

void ConnectUdpTunnel::SendUdpPacketToTarget(absl::string_view packet) {
  absl::Status send_result = target_socket_->SendBlocking(std::string(packet));
  if (!send_result.ok()) {
    QUICHE_LOG(WARNING) << "Error sending CONNECT-UDP datagram to target: "
                        << send_result;
  }
}

void ConnectUdpTunnel::SendConnectResponse() {
  QUICHE_DCHECK(IsTunnelOpenToTarget());
  QUICHE_DCHECK(client_stream_request_handler_);

  spdy::Http2HeaderBlock response_headers;
  response_headers[":status"] = "200";

  std::optional<std::string> capsule_protocol_value =
      structured_headers::SerializeItem(structured_headers::Item(true));
  QUICHE_CHECK(capsule_protocol_value.has_value());
  response_headers["Capsule-Protocol"] = *capsule_protocol_value;

  QuicBackendResponse response;
  response.set_headers(std::move(response_headers));
  // Need to leave the stream open after sending the CONNECT response.
  response.set_response_type(QuicBackendResponse::INCOMPLETE_RESPONSE);

  client_stream_request_handler_->OnResponseBackendComplete(&response);
}

void ConnectUdpTunnel::SendErrorResponse(absl::string_view status,
                                         absl::string_view proxy_status_error,
                                         absl::string_view error_details) {
  QUICHE_DCHECK(!status.empty());
  QUICHE_DCHECK(!proxy_status_error.empty());
  QUICHE_DCHECK(!error_details.empty());
  QUICHE_DCHECK(client_stream_request_handler_);

#ifndef NDEBUG
  // Expect a valid status code (number, 100 to 599 inclusive) and not a
  // Successful code (200 to 299 inclusive).
  int status_num = 0;
  bool is_num = absl::SimpleAtoi(status, &status_num);
  QUICHE_DCHECK(is_num);
  QUICHE_DCHECK_GE(status_num, 100);
  QUICHE_DCHECK_LT(status_num, 600);
  QUICHE_DCHECK(status_num < 200 || status_num >= 300);
#endif  // !NDEBUG

  spdy::Http2HeaderBlock headers;
  headers[":status"] = status;

  structured_headers::Item proxy_status_item(server_label_);
  structured_headers::Item proxy_status_error_item(
      std::string{proxy_status_error});
  structured_headers::Item proxy_status_details_item(
      std::string{error_details});
  structured_headers::ParameterizedMember proxy_status_member(
      std::move(proxy_status_item),
      {{"error", std::move(proxy_status_error_item)},
       {"details", std::move(proxy_status_details_item)}});
  std::optional<std::string> proxy_status_value =
      structured_headers::SerializeList({proxy_status_member});
  QUICHE_CHECK(proxy_status_value.has_value());
  headers["Proxy-Status"] = *proxy_status_value;

  QuicBackendResponse response;
  response.set_headers(std::move(headers));

  client_stream_request_handler_->OnResponseBackendComplete(&response);
}

void ConnectUdpTunnel::TerminateClientStream(
    absl::string_view error_description, QuicResetStreamError error_code) {
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
