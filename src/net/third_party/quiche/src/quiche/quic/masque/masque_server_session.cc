// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_server_session.h"

#include <fcntl.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "openssl/curve25519.h"
#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/frames/quic_connection_close_frame.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_udp_socket.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/masque/masque_server_backend.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_backend_response.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/quic/tools/quic_simple_server_session.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/capsule.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_url_utils.h"
#include "quiche/common/quiche_ip_address.h"
#include "quiche/common/quiche_text_utils.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

namespace {

using ::quiche::AddressAssignCapsule;
using ::quiche::AddressRequestCapsule;
using ::quiche::Capsule;
using ::quiche::IpAddressRange;
using ::quiche::PrefixWithId;
using ::quiche::RouteAdvertisementCapsule;

// RAII wrapper for QuicUdpSocketFd.
class FdWrapper {
 public:
  // Takes ownership of |fd| and closes the file descriptor on destruction.
  explicit FdWrapper(int address_family) {
    QuicUdpSocketApi socket_api;
    fd_ =
        socket_api.Create(address_family,
                          /*receive_buffer_size =*/kDefaultSocketReceiveBuffer,
                          /*send_buffer_size =*/kDefaultSocketReceiveBuffer);
  }

  ~FdWrapper() {
    if (fd_ == kQuicInvalidSocketFd) {
      return;
    }
    QuicUdpSocketApi socket_api;
    socket_api.Destroy(fd_);
  }

  // Hands ownership of the file descriptor to the caller.
  QuicUdpSocketFd extract_fd() {
    QuicUdpSocketFd fd = fd_;
    fd_ = kQuicInvalidSocketFd;
    return fd;
  }

  // Keeps ownership of the file descriptor.
  QuicUdpSocketFd fd() { return fd_; }

  // Disallow copy and move.
  FdWrapper(const FdWrapper&) = delete;
  FdWrapper(FdWrapper&&) = delete;
  FdWrapper& operator=(const FdWrapper&) = delete;
  FdWrapper& operator=(FdWrapper&&) = delete;

 private:
  QuicUdpSocketFd fd_;
};

std::unique_ptr<QuicBackendResponse> CreateBackendErrorResponse(
    absl::string_view status, absl::string_view error_details) {
  spdy::Http2HeaderBlock response_headers;
  response_headers[":status"] = status;
  response_headers["masque-debug-info"] = error_details;
  auto response = std::make_unique<QuicBackendResponse>();
  response->set_response_type(QuicBackendResponse::REGULAR_RESPONSE);
  response->set_headers(std::move(response_headers));
  return response;
}

}  // namespace

MasqueServerSession::MasqueServerSession(
    MasqueMode masque_mode, const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection, QuicSession::Visitor* visitor,
    QuicEventLoop* event_loop, QuicCryptoServerStreamBase::Helper* helper,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    MasqueServerBackend* masque_server_backend)
    : QuicSimpleServerSession(config, supported_versions, connection, visitor,
                              helper, crypto_config, compressed_certs_cache,
                              masque_server_backend),
      masque_server_backend_(masque_server_backend),
      event_loop_(event_loop),
      masque_mode_(masque_mode) {
  // Artificially increase the max packet length to ensure we can fit QUIC
  // packets inside DATAGRAM frames.
  // TODO(b/181606597) Remove this workaround once we use PMTUD.
  connection->SetMaxPacketLength(kDefaultMaxPacketSizeForTunnels);

  masque_server_backend_->RegisterBackendClient(connection_id(), this);
  QUICHE_DCHECK_NE(event_loop_, nullptr);

  // We don't currently use `masque_mode_` but will in the future. To silence
  // clang's `-Wunused-private-field` warning for this when building QUICHE for
  // Chrome, add a use of it here.
  (void)masque_mode_;
}

void MasqueServerSession::OnMessageAcked(QuicMessageId message_id,
                                         QuicTime /*receive_timestamp*/) {
  QUIC_DVLOG(1) << "Received ack for DATAGRAM frame " << message_id;
}

void MasqueServerSession::OnMessageLost(QuicMessageId message_id) {
  QUIC_DVLOG(1) << "We believe DATAGRAM frame " << message_id << " was lost";
}

void MasqueServerSession::OnConnectionClosed(
    const QuicConnectionCloseFrame& frame, ConnectionCloseSource source) {
  QuicSimpleServerSession::OnConnectionClosed(frame, source);
  QUIC_DLOG(INFO) << "Closing connection for " << connection_id();
  masque_server_backend_->RemoveBackendClient(connection_id());
  // Clearing this state will close all sockets.
  connect_udp_server_states_.clear();
}

void MasqueServerSession::OnStreamClosed(QuicStreamId stream_id) {
  connect_udp_server_states_.remove_if(
      [stream_id](const ConnectUdpServerState& connect_udp) {
        return connect_udp.stream()->id() == stream_id;
      });
  connect_ip_server_states_.remove_if(
      [stream_id](const ConnectIpServerState& connect_ip) {
        return connect_ip.stream()->id() == stream_id;
      });
  connect_ethernet_server_states_.remove_if(
      [stream_id](const ConnectEthernetServerState& connect_ethernet) {
        return connect_ethernet.stream()->id() == stream_id;
      });

  QuicSimpleServerSession::OnStreamClosed(stream_id);
}

std::unique_ptr<QuicBackendResponse>
MasqueServerSession::MaybeCheckSignatureAuth(
    const spdy::Http2HeaderBlock& request_headers, absl::string_view authority,
    absl::string_view scheme,
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  // TODO(dschinazi) Add command-line flag that makes this implementation
  // probe-resistant by returning the usual failure instead of 401.
  constexpr absl::string_view kSignatureAuthStatus = "401";
  if (!masque_server_backend_->IsSignatureAuthEnabled()) {
    return nullptr;
  }
  auto authorization_pair = request_headers.find("authorization");
  if (authorization_pair == request_headers.end()) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Missing authorization header");
  }
  absl::string_view credentials = authorization_pair->second;
  quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&credentials);
  std::vector<absl::string_view> v =
      absl::StrSplit(credentials, absl::MaxSplits(' ', 1));
  if (v.size() != 2) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Authorization header missing space");
  }
  absl::string_view auth_scheme = v[0];
  if (auth_scheme != "Signature") {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Unexpected auth scheme");
  }
  absl::string_view auth_parameters = v[1];
  std::vector<absl::string_view> auth_parameters_split =
      absl::StrSplit(auth_parameters, ',');
  std::optional<std::string> key_id;
  std::optional<std::string> header_public_key;
  std::optional<std::string> proof;
  std::optional<uint16_t> signature_scheme;
  std::optional<std::string> verification;
  for (absl::string_view auth_parameter : auth_parameters_split) {
    std::vector<absl::string_view> auth_parameter_split =
        absl::StrSplit(auth_parameter, absl::MaxSplits('=', 1));
    if (auth_parameter_split.size() != 2) {
      continue;
    }
    absl::string_view param_name = auth_parameter_split[0];
    quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&param_name);
    if (param_name.size() != 1) {
      // All currently known authentication parameters are one character long.
      continue;
    }
    absl::string_view param_value = auth_parameter_split[1];
    quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&param_value);
    std::string decoded_param;
    switch (param_name[0]) {
      case 'k': {
        if (key_id.has_value()) {
          return CreateBackendErrorResponse(kSignatureAuthStatus,
                                            "Duplicate k");
        }
        if (!absl::WebSafeBase64Unescape(param_value, &decoded_param)) {
          return CreateBackendErrorResponse(kSignatureAuthStatus,
                                            "Failed to base64 decode k");
        }
        key_id = decoded_param;
      } break;
      case 'a': {
        if (header_public_key.has_value()) {
          return CreateBackendErrorResponse(kSignatureAuthStatus,
                                            "Duplicate a");
        }
        if (!absl::WebSafeBase64Unescape(param_value, &decoded_param)) {
          return CreateBackendErrorResponse(kSignatureAuthStatus,
                                            "Failed to base64 decode a");
        }
        header_public_key = decoded_param;
      } break;
      case 'p': {
        if (proof.has_value()) {
          return CreateBackendErrorResponse(kSignatureAuthStatus,
                                            "Duplicate p");
        }
        if (!absl::WebSafeBase64Unescape(param_value, &decoded_param)) {
          return CreateBackendErrorResponse(kSignatureAuthStatus,
                                            "Failed to base64 decode p");
        }
        proof = decoded_param;
      } break;
      case 's': {
        if (signature_scheme.has_value()) {
          return CreateBackendErrorResponse(kSignatureAuthStatus,
                                            "Duplicate s");
        }
        int signature_scheme_int = 0;
        if (!absl::SimpleAtoi(param_value, &signature_scheme_int) ||
            signature_scheme_int < 0 ||
            signature_scheme_int > std::numeric_limits<uint16_t>::max()) {
          return CreateBackendErrorResponse(kSignatureAuthStatus,
                                            "Failed to parse s");
        }
        signature_scheme = static_cast<uint16_t>(signature_scheme_int);
      } break;
      case 'v': {
        if (verification.has_value()) {
          return CreateBackendErrorResponse(kSignatureAuthStatus,
                                            "Duplicate v");
        }
        if (!absl::WebSafeBase64Unescape(param_value, &decoded_param)) {
          return CreateBackendErrorResponse(kSignatureAuthStatus,
                                            "Failed to base64 decode v");
        }
        verification = decoded_param;
      } break;
    }
  }
  if (!key_id.has_value()) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Missing k auth parameter");
  }
  if (!header_public_key.has_value()) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Missing a auth parameter");
  }
  if (!proof.has_value()) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Missing p auth parameter");
  }
  if (!signature_scheme.has_value()) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Missing s auth parameter");
  }
  if (!verification.has_value()) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Missing v auth parameter");
  }
  uint8_t config_public_key[ED25519_PUBLIC_KEY_LEN];
  if (!masque_server_backend_->GetSignatureAuthKeyForId(*key_id,
                                                        config_public_key)) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Unexpected key id");
  }
  if (*header_public_key !=
      std::string(reinterpret_cast<const char*>(config_public_key),
                  sizeof(config_public_key))) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Unexpected public key in header");
  }
  std::string realm = "";
  QuicUrl url(absl::StrCat(scheme, "://", authority, "/"));
  std::optional<std::string> key_exporter_context = ComputeSignatureAuthContext(
      kEd25519SignatureScheme, *key_id, *header_public_key, scheme, url.host(),
      url.port(), realm);
  if (!key_exporter_context.has_value()) {
    return CreateBackendErrorResponse(
        "500", "Failed to generate key exporter context");
  }
  QUIC_DVLOG(1) << "key_exporter_context: "
                << absl::WebSafeBase64Escape(*key_exporter_context);
  QUICHE_DCHECK(!key_exporter_context->empty());
  std::string key_exporter_output;
  if (!GetMutableCryptoStream()->ExportKeyingMaterial(
          kSignatureAuthLabel, *key_exporter_context,
          kSignatureAuthExporterSize, &key_exporter_output)) {
    return CreateBackendErrorResponse("500", "Key exporter failed");
  }
  QUICHE_CHECK_EQ(key_exporter_output.size(), kSignatureAuthExporterSize);
  std::string signature_input =
      key_exporter_output.substr(0, kSignatureAuthSignatureInputSize);
  QUIC_DVLOG(1) << "signature_input: "
                << absl::WebSafeBase64Escape(signature_input);
  std::string expected_verification = key_exporter_output.substr(
      kSignatureAuthSignatureInputSize, kSignatureAuthVerificationSize);
  if (verification != expected_verification) {
    return CreateBackendErrorResponse(
        kSignatureAuthStatus,
        absl::StrCat("Unexpected verification, expected ",
                     absl::WebSafeBase64Escape(expected_verification),
                     " but got ", absl::WebSafeBase64Escape(*verification),
                     " - key exporter context was ",
                     absl::WebSafeBase64Escape(*key_exporter_context)));
  }
  std::string data_covered_by_signature =
      SignatureAuthDataCoveredBySignature(signature_input);
  QUIC_DVLOG(1) << "data_covered_by_signature: "
                << absl::WebSafeBase64Escape(data_covered_by_signature);
  if (*signature_scheme != kEd25519SignatureScheme) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Unexpected signature scheme");
  }
  if (proof->size() != ED25519_SIGNATURE_LEN) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Unexpected proof length");
  }
  if (ED25519_verify(
          reinterpret_cast<const uint8_t*>(data_covered_by_signature.data()),
          data_covered_by_signature.size(),
          reinterpret_cast<const uint8_t*>(proof->data()),
          config_public_key) != 1) {
    return CreateBackendErrorResponse(kSignatureAuthStatus,
                                      "Signature failed to validate");
  }
  QUIC_LOG(INFO) << "Successfully validated signature auth for stream ID "
                 << request_handler->stream_id();
  return nullptr;
}

std::unique_ptr<QuicBackendResponse> MasqueServerSession::HandleMasqueRequest(
    const spdy::Http2HeaderBlock& request_headers,
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  // Authority.
  auto authority_pair = request_headers.find(":authority");
  if (authority_pair == request_headers.end()) {
    QUIC_DLOG(ERROR) << "MASQUE request is missing :authority";
    return CreateBackendErrorResponse("400", "Missing :authority");
  }
  absl::string_view authority = authority_pair->second;
  // Scheme.
  auto scheme_pair = request_headers.find(":scheme");
  if (scheme_pair == request_headers.end()) {
    QUIC_DLOG(ERROR) << "MASQUE request is missing :scheme";
    return CreateBackendErrorResponse("400", "Missing :scheme");
  }
  absl::string_view scheme = scheme_pair->second;
  if (scheme.empty()) {
    return CreateBackendErrorResponse("400", "Empty scheme");
  }
  // Signature authentication.
  auto signature_auth_reply = MaybeCheckSignatureAuth(
      request_headers, authority, scheme, request_handler);
  if (signature_auth_reply) {
    return signature_auth_reply;
  }
  // Path.
  auto path_pair = request_headers.find(":path");
  if (path_pair == request_headers.end()) {
    QUIC_DLOG(ERROR) << "MASQUE request is missing :path";
    return CreateBackendErrorResponse("400", "Missing :path");
  }
  absl::string_view path = path_pair->second;
  if (path.empty()) {
    QUIC_DLOG(ERROR) << "MASQUE request with empty path";
    return CreateBackendErrorResponse("400", "Empty path");
  }
  // Method.
  auto method_pair = request_headers.find(":method");
  if (method_pair == request_headers.end()) {
    QUIC_DLOG(ERROR) << "MASQUE request is missing :method";
    return CreateBackendErrorResponse("400", "Missing :method");
  }
  absl::string_view method = method_pair->second;
  if (method != "CONNECT") {
    QUIC_DLOG(ERROR) << "MASQUE request with bad method \"" << method << "\"";
    if (masque_server_backend_->IsSignatureAuthOnAllRequests()) {
      return nullptr;
    } else {
      return CreateBackendErrorResponse("400", "Bad method");
    }
  }
  // Protocol.
  auto protocol_pair = request_headers.find(":protocol");
  if (protocol_pair == request_headers.end()) {
    QUIC_DLOG(ERROR) << "MASQUE request is missing :protocol";
    if (masque_server_backend_->IsSignatureAuthOnAllRequests()) {
      return nullptr;
    } else {
      return CreateBackendErrorResponse("400", "Missing :protocol");
    }
  }
  absl::string_view protocol = protocol_pair->second;
  if (protocol != "connect-udp" && protocol != "connect-ip" &&
      protocol != "connect-ethernet") {
    QUIC_DLOG(ERROR) << "MASQUE request with bad protocol \"" << protocol
                     << "\"";
    if (masque_server_backend_->IsSignatureAuthOnAllRequests()) {
      return nullptr;
    } else {
      return CreateBackendErrorResponse("400", "Bad protocol");
    }
  }

  if (protocol == "connect-ip") {
    QuicSpdyStream* stream = static_cast<QuicSpdyStream*>(
        GetActiveStream(request_handler->stream_id()));
    if (stream == nullptr) {
      QUIC_BUG(bad masque server stream type)
          << "Unexpected stream type for stream ID "
          << request_handler->stream_id();
      return CreateBackendErrorResponse("500", "Bad stream type");
    }
    QuicIpAddress client_ip = masque_server_backend_->GetNextClientIpAddress();
    QUIC_DLOG(INFO) << "Using client IP " << client_ip.ToString()
                    << " for CONNECT-IP stream ID "
                    << request_handler->stream_id();
    int fd = CreateTunInterface(client_ip);
    if (fd < 0) {
      QUIC_LOG(ERROR) << "Failed to create TUN interface for stream ID "
                      << request_handler->stream_id();
      return CreateBackendErrorResponse("500",
                                        "Failed to create TUN interface");
    }
    if (!event_loop_->RegisterSocket(fd, kSocketEventReadable, this)) {
      QUIC_DLOG(ERROR) << "Failed to register TUN fd with the event loop";
      close(fd);
      return CreateBackendErrorResponse("500", "Registering TUN socket failed");
    }
    connect_ip_server_states_.push_back(
        ConnectIpServerState(client_ip, stream, fd, this));

    spdy::Http2HeaderBlock response_headers;
    response_headers[":status"] = "200";
    auto response = std::make_unique<QuicBackendResponse>();
    response->set_response_type(QuicBackendResponse::INCOMPLETE_RESPONSE);
    response->set_headers(std::move(response_headers));
    response->set_body("");

    return response;
  }
  if (protocol == "connect-ethernet") {
    QuicSpdyStream* stream = static_cast<QuicSpdyStream*>(
        GetActiveStream(request_handler->stream_id()));
    if (stream == nullptr) {
      QUIC_BUG(bad masque server stream type)
          << "Unexpected stream type for stream ID "
          << request_handler->stream_id();
      return CreateBackendErrorResponse("500", "Bad stream type");
    }
    int fd = CreateTapInterface();
    if (fd < 0) {
      QUIC_LOG(ERROR) << "Failed to create TAP interface for stream ID "
                      << request_handler->stream_id();
      return CreateBackendErrorResponse("500",
                                        "Failed to create TAP interface");
    }
    if (!event_loop_->RegisterSocket(fd, kSocketEventReadable, this)) {
      QUIC_DLOG(ERROR) << "Failed to register TAP fd with the event loop";
      close(fd);
      return CreateBackendErrorResponse("500", "Registering TAP socket failed");
    }
    connect_ethernet_server_states_.push_back(
        ConnectEthernetServerState(stream, fd, this));

    spdy::Http2HeaderBlock response_headers;
    response_headers[":status"] = "200";
    auto response = std::make_unique<QuicBackendResponse>();
    response->set_response_type(QuicBackendResponse::INCOMPLETE_RESPONSE);
    response->set_headers(std::move(response_headers));
    response->set_body("");

    return response;
  }
  // Extract target host and port from path using default template.
  std::vector<absl::string_view> path_split = absl::StrSplit(path, '/');
  if (path_split.size() != 7 || !path_split[0].empty() ||
      path_split[1] != ".well-known" || path_split[2] != "masque" ||
      path_split[3] != "udp" || path_split[4].empty() ||
      path_split[5].empty() || !path_split[6].empty()) {
    QUIC_DLOG(ERROR) << "MASQUE request with bad path \"" << path << "\"";
    return CreateBackendErrorResponse("400", "Bad path");
  }
  std::optional<std::string> host = quiche::AsciiUrlDecode(path_split[4]);
  if (!host.has_value()) {
    QUIC_DLOG(ERROR) << "Failed to decode host \"" << path_split[4] << "\"";
    return CreateBackendErrorResponse("500", "Failed to decode host");
  }
  std::optional<std::string> port = quiche::AsciiUrlDecode(path_split[5]);
  if (!port.has_value()) {
    QUIC_DLOG(ERROR) << "Failed to decode port \"" << path_split[5] << "\"";
    return CreateBackendErrorResponse("500", "Failed to decode port");
  }

  // Perform DNS resolution.
  addrinfo hint = {};
  hint.ai_protocol = IPPROTO_UDP;

  addrinfo* info_list = nullptr;
  int result = getaddrinfo(host->c_str(), port->c_str(), &hint, &info_list);
  if (result != 0 || info_list == nullptr) {
    QUIC_DLOG(ERROR) << "Failed to resolve " << authority << ": "
                     << gai_strerror(result);
    return CreateBackendErrorResponse("500", "DNS resolution failed");
  }

  std::unique_ptr<addrinfo, void (*)(addrinfo*)> info_list_owned(info_list,
                                                                 freeaddrinfo);
  QuicSocketAddress target_server_address(info_list->ai_addr,
                                          info_list->ai_addrlen);
  QUIC_DLOG(INFO) << "Got CONNECT_UDP request on stream ID "
                  << request_handler->stream_id() << " target_server_address=\""
                  << target_server_address << "\"";

  FdWrapper fd_wrapper(target_server_address.host().AddressFamilyToInt());
  if (fd_wrapper.fd() == kQuicInvalidSocketFd) {
    QUIC_DLOG(ERROR) << "Socket creation failed";
    return CreateBackendErrorResponse("500", "Socket creation failed");
  }
  QuicSocketAddress empty_address(QuicIpAddress::Any6(), 0);
  if (target_server_address.host().IsIPv4()) {
    empty_address = QuicSocketAddress(QuicIpAddress::Any4(), 0);
  }
  QuicUdpSocketApi socket_api;
  if (!socket_api.Bind(fd_wrapper.fd(), empty_address)) {
    QUIC_DLOG(ERROR) << "Socket bind failed";
    return CreateBackendErrorResponse("500", "Socket bind failed");
  }
  if (!event_loop_->RegisterSocket(fd_wrapper.fd(), kSocketEventReadable,
                                   this)) {
    QUIC_DLOG(ERROR) << "Failed to register socket with the event loop";
    return CreateBackendErrorResponse("500", "Registering socket failed");
  }

  QuicSpdyStream* stream =
      static_cast<QuicSpdyStream*>(GetActiveStream(request_handler->stream_id()));
  if (stream == nullptr) {
    QUIC_BUG(bad masque server stream type)
        << "Unexpected stream type for stream ID "
        << request_handler->stream_id();
    return CreateBackendErrorResponse("500", "Bad stream type");
  }
  connect_udp_server_states_.push_back(ConnectUdpServerState(
      stream, target_server_address, fd_wrapper.extract_fd(), this));

  spdy::Http2HeaderBlock response_headers;
  response_headers[":status"] = "200";
  auto response = std::make_unique<QuicBackendResponse>();
  response->set_response_type(QuicBackendResponse::INCOMPLETE_RESPONSE);
  response->set_headers(std::move(response_headers));
  response->set_body("");

  return response;
}

void MasqueServerSession::OnSocketEvent(QuicEventLoop* /*event_loop*/,
                                        QuicUdpSocketFd fd,
                                        QuicSocketEventMask events) {
  if ((events & kSocketEventReadable) == 0) {
    QUIC_DVLOG(1) << "Ignoring OnEvent fd " << fd << " event mask " << events;
    return;
  }

  auto rearm = absl::MakeCleanup([&]() {
    if (!event_loop_->SupportsEdgeTriggered()) {
      if (!event_loop_->RearmSocket(fd, kSocketEventReadable)) {
        QUIC_BUG(MasqueServerSession_OnSocketEvent_Rearm)
            << "Failed to re-arm socket " << fd << " for reading";
      }
    }
  });

  if (!(HandleConnectUdpSocketEvent(fd, events) ||
        HandleConnectIpSocketEvent(fd, events) ||
        HandleConnectEthernetSocketEvent(fd, events))) {
    QUIC_BUG(MasqueServerSession_OnSocketEvent_UnhandledEvent)
        << "Got unexpected event mask " << events << " on unknown fd " << fd;
    std::move(rearm).Cancel();
  }
}

bool MasqueServerSession::HandleConnectUdpSocketEvent(
    QuicUdpSocketFd fd, QuicSocketEventMask events) {
  auto it = absl::c_find_if(connect_udp_server_states_,
                            [fd](const ConnectUdpServerState& connect_udp) {
                              return connect_udp.fd() == fd;
                            });
  if (it == connect_udp_server_states_.end()) {
    return false;
  }
  QuicSocketAddress expected_target_server_address =
      it->target_server_address();
  QUICHE_DCHECK(expected_target_server_address.IsInitialized());
  QUIC_DVLOG(1) << "Received readable event on fd " << fd << " (mask " << events
                << ") stream ID " << it->stream()->id() << " server "
                << expected_target_server_address;
  QuicUdpSocketApi socket_api;
  QuicUdpPacketInfoBitMask packet_info_interested(
      {QuicUdpPacketInfoBit::PEER_ADDRESS});
  char packet_buffer[1 + kMaxIncomingPacketSize];
  packet_buffer[0] = 0;  // context ID.
  char control_buffer[kDefaultUdpPacketControlBufferSize];
  while (true) {
    QuicUdpSocketApi::ReadPacketResult read_result;
    read_result.packet_buffer = {packet_buffer + 1, sizeof(packet_buffer) - 1};
    read_result.control_buffer = {control_buffer, sizeof(control_buffer)};
    socket_api.ReadPacket(fd, packet_info_interested, &read_result);
    if (!read_result.ok) {
      // Most likely there is nothing left to read, break out of read loop.
      break;
    }
    if (!read_result.packet_info.HasValue(QuicUdpPacketInfoBit::PEER_ADDRESS)) {
      QUIC_BUG(MasqueServerSession_HandleConnectUdpSocketEvent_MissingPeer)
          << "Missing peer address when reading from fd " << fd;
      continue;
    }
    if (read_result.packet_info.peer_address() !=
        expected_target_server_address) {
      QUIC_DLOG(ERROR) << "Ignoring UDP packet on fd " << fd
                       << " from unexpected server address "
                       << read_result.packet_info.peer_address()
                       << " (expected " << expected_target_server_address
                       << ")";
      continue;
    }
    if (!connection()->connected()) {
      QUIC_BUG(MasqueServerSession_HandleConnectUdpSocketEvent_ConnectionClosed)
          << "Unexpected incoming UDP packet on fd " << fd << " from "
          << expected_target_server_address
          << " because MASQUE connection is closed";
      return true;
    }
    // The packet is valid, send it to the client in a DATAGRAM frame.
    MessageStatus message_status =
        it->stream()->SendHttp3Datagram(absl::string_view(
            packet_buffer, read_result.packet_buffer.buffer_len + 1));
    QUIC_DVLOG(1) << "Sent UDP packet from " << expected_target_server_address
                  << " of length " << read_result.packet_buffer.buffer_len
                  << " with stream ID " << it->stream()->id()
                  << " and got message status "
                  << MessageStatusToString(message_status);
  }
  return true;
}

bool MasqueServerSession::HandleConnectIpSocketEvent(
    QuicUdpSocketFd fd, QuicSocketEventMask events) {
  auto it = absl::c_find_if(connect_ip_server_states_,
                            [fd](const ConnectIpServerState& connect_ip) {
                              return connect_ip.fd() == fd;
                            });
  if (it == connect_ip_server_states_.end()) {
    return false;
  }
  QUIC_DVLOG(1) << "Received readable event on fd " << fd << " (mask " << events
                << ") stream ID " << it->stream()->id();
  char datagram[kMasqueIpPacketBufferSize];
  datagram[0] = 0;  // Context ID.
  while (true) {
    ssize_t read_size = read(fd, datagram + 1, sizeof(datagram) - 1);
    if (read_size < 0) {
      break;
    }
    MessageStatus message_status = it->stream()->SendHttp3Datagram(
        absl::string_view(datagram, 1 + read_size));
    QUIC_DVLOG(1) << "Encapsulated IP packet of length " << read_size
                  << " with stream ID " << it->stream()->id()
                  << " and got message status "
                  << MessageStatusToString(message_status);
  }
  return true;
}

bool MasqueServerSession::HandleConnectEthernetSocketEvent(
    QuicUdpSocketFd fd, QuicSocketEventMask events) {
  auto it =
      absl::c_find_if(connect_ethernet_server_states_,
                      [fd](const ConnectEthernetServerState& connect_ethernet) {
                        return connect_ethernet.fd() == fd;
                      });
  if (it == connect_ethernet_server_states_.end()) {
    return false;
  }
  QUIC_DVLOG(1) << "Received readable event on fd " << fd << " (mask " << events
                << ") stream ID " << it->stream()->id();
  char datagram[kMasqueEthernetFrameBufferSize];
  datagram[0] = 0;  // Context ID.
  while (true) {
    ssize_t read_size = read(fd, datagram + 1, sizeof(datagram) - 1);
    if (read_size < 0) {
      break;
    }
    MessageStatus message_status = it->stream()->SendHttp3Datagram(
        absl::string_view(datagram, 1 + read_size));
    QUIC_DVLOG(1) << "Encapsulated Ethernet frame of length " << read_size
                  << " with stream ID " << it->stream()->id()
                  << " and got message status "
                  << MessageStatusToString(message_status);
  }
  return true;
}

bool MasqueServerSession::OnSettingsFrame(const SettingsFrame& frame) {
  QUIC_DLOG(INFO) << "Received SETTINGS: " << frame;
  if (!QuicSimpleServerSession::OnSettingsFrame(frame)) {
    return false;
  }
  if (!SupportsH3Datagram()) {
    QUIC_DLOG(ERROR) << "Refusing to use MASQUE without HTTP Datagrams";
    return false;
  }
  QUIC_DLOG(INFO) << "Using HTTP Datagram: " << http_datagram_support();
  return true;
}

MasqueServerSession::ConnectUdpServerState::ConnectUdpServerState(
    QuicSpdyStream* stream, const QuicSocketAddress& target_server_address,
    QuicUdpSocketFd fd, MasqueServerSession* masque_session)
    : stream_(stream),
      target_server_address_(target_server_address),
      fd_(fd),
      masque_session_(masque_session) {
  QUICHE_DCHECK_NE(fd_, kQuicInvalidSocketFd);
  QUICHE_DCHECK_NE(masque_session_, nullptr);
  this->stream()->RegisterHttp3DatagramVisitor(this);
}

MasqueServerSession::ConnectUdpServerState::~ConnectUdpServerState() {
  if (stream() != nullptr) {
    stream()->UnregisterHttp3DatagramVisitor();
  }
  if (fd_ == kQuicInvalidSocketFd) {
    return;
  }
  QuicUdpSocketApi socket_api;
  QUIC_DLOG(INFO) << "Closing fd " << fd_;
  if (!masque_session_->event_loop()->UnregisterSocket(fd_)) {
    QUIC_DLOG(ERROR) << "Failed to unregister FD " << fd_;
  }
  socket_api.Destroy(fd_);
}

MasqueServerSession::ConnectUdpServerState::ConnectUdpServerState(
    MasqueServerSession::ConnectUdpServerState&& other) {
  fd_ = kQuicInvalidSocketFd;
  *this = std::move(other);
}

MasqueServerSession::ConnectUdpServerState&
MasqueServerSession::ConnectUdpServerState::operator=(
    MasqueServerSession::ConnectUdpServerState&& other) {
  if (fd_ != kQuicInvalidSocketFd) {
    QuicUdpSocketApi socket_api;
    QUIC_DLOG(INFO) << "Closing fd " << fd_;
    if (!masque_session_->event_loop()->UnregisterSocket(fd_)) {
      QUIC_DLOG(ERROR) << "Failed to unregister FD " << fd_;
    }
    socket_api.Destroy(fd_);
  }
  stream_ = other.stream_;
  other.stream_ = nullptr;
  target_server_address_ = other.target_server_address_;
  fd_ = other.fd_;
  masque_session_ = other.masque_session_;
  other.fd_ = kQuicInvalidSocketFd;
  if (stream() != nullptr) {
    stream()->ReplaceHttp3DatagramVisitor(this);
  }
  return *this;
}

void MasqueServerSession::ConnectUdpServerState::OnHttp3Datagram(
    QuicStreamId stream_id, absl::string_view payload) {
  QUICHE_DCHECK_EQ(stream_id, stream()->id());
  QuicDataReader reader(payload);
  uint64_t context_id;
  if (!reader.ReadVarInt62(&context_id)) {
    QUIC_DLOG(ERROR) << "Failed to read context ID";
    return;
  }
  if (context_id != 0) {
    QUIC_DLOG(ERROR) << "Ignoring HTTP Datagram with unexpected context ID "
                     << context_id;
    return;
  }
  absl::string_view http_payload = reader.ReadRemainingPayload();
  QuicUdpSocketApi socket_api;
  QuicUdpPacketInfo packet_info;
  packet_info.SetPeerAddress(target_server_address_);
  WriteResult write_result = socket_api.WritePacket(
      fd_, http_payload.data(), http_payload.length(), packet_info);
  QUIC_DVLOG(1) << "Wrote packet of length " << http_payload.length() << " to "
                << target_server_address_ << " with result " << write_result;
}

MasqueServerSession::ConnectIpServerState::ConnectIpServerState(
    QuicIpAddress client_ip, QuicSpdyStream* stream, QuicUdpSocketFd fd,
    MasqueServerSession* masque_session)
    : client_ip_(client_ip),
      stream_(stream),
      fd_(fd),
      masque_session_(masque_session) {
  QUICHE_DCHECK(client_ip_.IsIPv4());
  QUICHE_DCHECK_NE(fd_, kQuicInvalidSocketFd);
  QUICHE_DCHECK_NE(masque_session_, nullptr);
  this->stream()->RegisterHttp3DatagramVisitor(this);
  this->stream()->RegisterConnectIpVisitor(this);
}

MasqueServerSession::ConnectIpServerState::~ConnectIpServerState() {
  if (stream() != nullptr) {
    stream()->UnregisterHttp3DatagramVisitor();
    stream()->UnregisterConnectIpVisitor();
  }
  if (fd_ == kQuicInvalidSocketFd) {
    return;
  }
  QuicUdpSocketApi socket_api;
  QUIC_DLOG(INFO) << "Closing fd " << fd_;
  if (!masque_session_->event_loop()->UnregisterSocket(fd_)) {
    QUIC_DLOG(ERROR) << "Failed to unregister FD " << fd_;
  }
  socket_api.Destroy(fd_);
}

MasqueServerSession::ConnectIpServerState::ConnectIpServerState(
    MasqueServerSession::ConnectIpServerState&& other) {
  fd_ = kQuicInvalidSocketFd;
  *this = std::move(other);
}

MasqueServerSession::ConnectIpServerState&
MasqueServerSession::ConnectIpServerState::operator=(
    MasqueServerSession::ConnectIpServerState&& other) {
  if (fd_ != kQuicInvalidSocketFd) {
    QuicUdpSocketApi socket_api;
    QUIC_DLOG(INFO) << "Closing fd " << fd_;
    if (!masque_session_->event_loop()->UnregisterSocket(fd_)) {
      QUIC_DLOG(ERROR) << "Failed to unregister FD " << fd_;
    }
    socket_api.Destroy(fd_);
  }
  client_ip_ = other.client_ip_;
  stream_ = other.stream_;
  other.stream_ = nullptr;
  fd_ = other.fd_;
  masque_session_ = other.masque_session_;
  other.fd_ = kQuicInvalidSocketFd;
  if (stream() != nullptr) {
    stream()->ReplaceHttp3DatagramVisitor(this);
    stream()->ReplaceConnectIpVisitor(this);
  }
  return *this;
}

void MasqueServerSession::ConnectIpServerState::OnHttp3Datagram(
    QuicStreamId stream_id, absl::string_view payload) {
  QUICHE_DCHECK_EQ(stream_id, stream()->id());
  QuicDataReader reader(payload);
  uint64_t context_id;
  if (!reader.ReadVarInt62(&context_id)) {
    QUIC_DLOG(ERROR) << "Failed to read context ID";
    return;
  }
  if (context_id != 0) {
    QUIC_DLOG(ERROR) << "Ignoring HTTP Datagram with unexpected context ID "
                     << context_id;
    return;
  }
  absl::string_view ip_packet = reader.ReadRemainingPayload();
  ssize_t written = write(fd(), ip_packet.data(), ip_packet.size());
  if (written != static_cast<ssize_t>(ip_packet.size())) {
    QUIC_DLOG(ERROR) << "Failed to write CONNECT-IP packet of length "
                     << ip_packet.size();
  } else {
    QUIC_DLOG(INFO) << "Decapsulated CONNECT-IP packet of length "
                    << ip_packet.size();
  }
}

bool MasqueServerSession::ConnectIpServerState::OnAddressAssignCapsule(
    const AddressAssignCapsule& capsule) {
  QUIC_DLOG(INFO) << "Ignoring received capsule " << capsule.ToString();
  return true;
}

bool MasqueServerSession::ConnectIpServerState::OnAddressRequestCapsule(
    const AddressRequestCapsule& capsule) {
  QUIC_DLOG(INFO) << "Ignoring received capsule " << capsule.ToString();
  return true;
}

bool MasqueServerSession::ConnectIpServerState::OnRouteAdvertisementCapsule(
    const RouteAdvertisementCapsule& capsule) {
  QUIC_DLOG(INFO) << "Ignoring received capsule " << capsule.ToString();
  return true;
}

void MasqueServerSession::ConnectIpServerState::OnHeadersWritten() {
  QUICHE_DCHECK(client_ip_.IsIPv4()) << client_ip_.ToString();
  Capsule address_assign_capsule = Capsule::AddressAssign();
  PrefixWithId assigned_address;
  assigned_address.ip_prefix = quiche::QuicheIpPrefix(client_ip_, 32);
  assigned_address.request_id = 0;
  address_assign_capsule.address_assign_capsule().assigned_addresses.push_back(
      assigned_address);
  stream()->WriteCapsule(address_assign_capsule);
  IpAddressRange default_route;
  default_route.start_ip_address.FromString("0.0.0.0");
  default_route.end_ip_address.FromString("255.255.255.255");
  default_route.ip_protocol = 0;
  Capsule route_advertisement = Capsule::RouteAdvertisement();
  route_advertisement.route_advertisement_capsule().ip_address_ranges.push_back(
      default_route);
  stream()->WriteCapsule(route_advertisement);
}

// Connect Ethernet
MasqueServerSession::ConnectEthernetServerState::ConnectEthernetServerState(
    QuicSpdyStream* stream, QuicUdpSocketFd fd,
    MasqueServerSession* masque_session)
    : stream_(stream), fd_(fd), masque_session_(masque_session) {
  QUICHE_DCHECK_NE(fd_, kQuicInvalidSocketFd);
  QUICHE_DCHECK_NE(masque_session_, nullptr);
  this->stream()->RegisterHttp3DatagramVisitor(this);
}

MasqueServerSession::ConnectEthernetServerState::~ConnectEthernetServerState() {
  if (stream() != nullptr) {
    stream()->UnregisterHttp3DatagramVisitor();
  }
  if (fd_ == kQuicInvalidSocketFd) {
    return;
  }
  QuicUdpSocketApi socket_api;
  QUIC_DLOG(INFO) << "Closing fd " << fd_;
  if (!masque_session_->event_loop()->UnregisterSocket(fd_)) {
    QUIC_DLOG(ERROR) << "Failed to unregister FD " << fd_;
  }
  socket_api.Destroy(fd_);
}

MasqueServerSession::ConnectEthernetServerState::ConnectEthernetServerState(
    MasqueServerSession::ConnectEthernetServerState&& other) {
  fd_ = kQuicInvalidSocketFd;
  *this = std::move(other);
}

MasqueServerSession::ConnectEthernetServerState&
MasqueServerSession::ConnectEthernetServerState::operator=(
    MasqueServerSession::ConnectEthernetServerState&& other) {
  if (fd_ != kQuicInvalidSocketFd) {
    QuicUdpSocketApi socket_api;
    QUIC_DLOG(INFO) << "Closing fd " << fd_;
    if (!masque_session_->event_loop()->UnregisterSocket(fd_)) {
      QUIC_DLOG(ERROR) << "Failed to unregister FD " << fd_;
    }
    socket_api.Destroy(fd_);
  }
  stream_ = other.stream_;
  other.stream_ = nullptr;
  fd_ = other.fd_;
  masque_session_ = other.masque_session_;
  other.fd_ = kQuicInvalidSocketFd;
  if (stream() != nullptr) {
    stream()->ReplaceHttp3DatagramVisitor(this);
  }
  return *this;
}

void MasqueServerSession::ConnectEthernetServerState::OnHttp3Datagram(
    QuicStreamId stream_id, absl::string_view payload) {
  QUICHE_DCHECK_EQ(stream_id, stream()->id());
  QuicDataReader reader(payload);
  uint64_t context_id;
  if (!reader.ReadVarInt62(&context_id)) {
    QUIC_DLOG(ERROR) << "Failed to read context ID";
    return;
  }
  if (context_id != 0) {
    QUIC_DLOG(ERROR) << "Ignoring HTTP Datagram with unexpected context ID "
                     << context_id;
    return;
  }
  absl::string_view ethernet_frame = reader.ReadRemainingPayload();
  ssize_t written = write(fd(), ethernet_frame.data(), ethernet_frame.size());
  if (written != static_cast<ssize_t>(ethernet_frame.size())) {
    QUIC_DLOG(ERROR) << "Failed to write CONNECT-ETHERNET packet of length "
                     << ethernet_frame.size();
  } else {
    QUIC_DLOG(INFO) << "Decapsulated CONNECT-ETHERNET packet of length "
                    << ethernet_frame.size();
  }
}

}  // namespace quic
