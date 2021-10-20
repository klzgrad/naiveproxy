// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/masque/masque_server_session.h"

#include <netdb.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quic/core/http/spdy_utils.h"
#include "quic/core/quic_data_reader.h"
#include "quic/core/quic_udp_socket.h"
#include "quic/tools/quic_url.h"

namespace quic {

namespace {
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
    QuicConnection* connection, QuicSession::Visitor* visitor, Visitor* owner,
    QuicEpollServer* epoll_server, QuicCryptoServerStreamBase::Helper* helper,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    MasqueServerBackend* masque_server_backend)
    : QuicSimpleServerSession(config, supported_versions, connection, visitor,
                              helper, crypto_config, compressed_certs_cache,
                              masque_server_backend),
      masque_server_backend_(masque_server_backend),
      owner_(owner),
      epoll_server_(epoll_server),
      compression_engine_(this),
      masque_mode_(masque_mode) {
  // Artificially increase the max packet length to 1350 to ensure we can fit
  // QUIC packets inside DATAGRAM frames.
  // TODO(b/181606597) Remove this workaround once we use PMTUD.
  connection->SetMaxPacketLength(kDefaultMaxPacketSize);

  masque_server_backend_->RegisterBackendClient(connection_id(), this);
  QUICHE_DCHECK_NE(epoll_server_, nullptr);
}

void MasqueServerSession::OnMessageReceived(absl::string_view message) {
  if (masque_mode_ == MasqueMode::kLegacy) {
    QUIC_DVLOG(1) << "Received DATAGRAM frame of length " << message.length();
    QuicConnectionId client_connection_id, server_connection_id;
    QuicSocketAddress target_server_address;
    std::vector<char> packet;
    bool version_present;
    if (!compression_engine_.DecompressDatagram(
            message, &client_connection_id, &server_connection_id,
            &target_server_address, &packet, &version_present)) {
      return;
    }

    QUIC_DVLOG(1) << "Received packet of length " << packet.size() << " for "
                  << target_server_address << " client "
                  << client_connection_id;

    if (version_present) {
      if (client_connection_id.length() != kQuicDefaultConnectionIdLength) {
        QUIC_DLOG(ERROR)
            << "Dropping long header with invalid client_connection_id "
            << client_connection_id;
        return;
      }
      owner_->RegisterClientConnectionId(client_connection_id, this);
    }

    WriteResult write_result = connection()->writer()->WritePacket(
        packet.data(), packet.size(), connection()->self_address().host(),
        target_server_address, nullptr);
    QUIC_DVLOG(1) << "Got " << write_result << " for " << packet.size()
                  << " bytes to " << target_server_address;
    return;
  }
  QUICHE_DCHECK_EQ(masque_mode_, MasqueMode::kOpen);
  QuicSpdySession::OnMessageReceived(message);
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

  QuicSimpleServerSession::OnStreamClosed(stream_id);
}

std::unique_ptr<QuicBackendResponse> MasqueServerSession::HandleMasqueRequest(
    const std::string& masque_path,
    const spdy::Http2HeaderBlock& request_headers,
    const std::string& request_body,
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  if (masque_mode_ != MasqueMode::kLegacy) {
    auto path_pair = request_headers.find(":path");
    auto scheme_pair = request_headers.find(":scheme");
    auto method_pair = request_headers.find(":method");
    auto authority_pair = request_headers.find(":authority");
    if (path_pair == request_headers.end()) {
      QUIC_DLOG(ERROR) << "MASQUE request is missing :path";
      return CreateBackendErrorResponse("400", "Missing :path");
    }
    if (scheme_pair == request_headers.end()) {
      QUIC_DLOG(ERROR) << "MASQUE request is missing :scheme";
      return CreateBackendErrorResponse("400", "Missing :scheme");
    }
    if (method_pair == request_headers.end()) {
      QUIC_DLOG(ERROR) << "MASQUE request is missing :method";
      return CreateBackendErrorResponse("400", "Missing :method");
    }
    if (authority_pair == request_headers.end()) {
      QUIC_DLOG(ERROR) << "MASQUE request is missing :authority";
      return CreateBackendErrorResponse("400", "Missing :authority");
    }
    absl::string_view path = path_pair->second;
    absl::string_view scheme = scheme_pair->second;
    absl::string_view method = method_pair->second;
    absl::string_view authority = authority_pair->second;
    if (path.empty()) {
      QUIC_DLOG(ERROR) << "MASQUE request with empty path";
      return CreateBackendErrorResponse("400", "Empty path");
    }
    if (scheme.empty()) {
      return CreateBackendErrorResponse("400", "Empty scheme");
    }
    if (method != "CONNECT-UDP") {
      QUIC_DLOG(ERROR) << "MASQUE request with bad method \"" << method << "\"";
      return CreateBackendErrorResponse("400", "Bad method");
    }
    absl::optional<QuicDatagramStreamId> flow_id =
        SpdyUtils::ParseDatagramFlowIdHeader(request_headers);
    if (!flow_id.has_value()) {
      QUIC_DLOG(ERROR)
          << "MASQUE request with bad or missing DatagramFlowId header";
      return CreateBackendErrorResponse("400",
                                        "Bad or missing DatagramFlowId header");
    }
    QuicUrl url(absl::StrCat("https://", authority));
    if (!url.IsValid() || url.PathParamsQuery() != "/") {
      QUIC_DLOG(ERROR) << "MASQUE request with bad authority \"" << authority
                       << "\"";
      return CreateBackendErrorResponse("400", "Bad authority");
    }

    std::string port = absl::StrCat(url.port());
    addrinfo hint = {};
    hint.ai_protocol = IPPROTO_UDP;

    addrinfo* info_list = nullptr;
    int result =
        getaddrinfo(url.host().c_str(), port.c_str(), &hint, &info_list);
    if (result != 0) {
      QUIC_DLOG(ERROR) << "Failed to resolve " << authority << ": "
                       << gai_strerror(result);
      return CreateBackendErrorResponse("500", "DNS resolution failed");
    }

    QUICHE_CHECK_NE(info_list, nullptr);
    std::unique_ptr<addrinfo, void (*)(addrinfo*)> info_list_owned(
        info_list, freeaddrinfo);
    QuicSocketAddress target_server_address(info_list->ai_addr,
                                            info_list->ai_addrlen);
    QUIC_DLOG(INFO) << "Got CONNECT_UDP request flow_id=" << *flow_id
                    << " target_server_address=\"" << target_server_address
                    << "\"";

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
    epoll_server_->RegisterFDForRead(fd_wrapper.fd(), this);

    absl::optional<QuicDatagramContextId> context_id;
    QuicSpdyStream* stream = static_cast<QuicSpdyStream*>(
        GetActiveStream(request_handler->stream_id()));
    if (stream == nullptr) {
      QUIC_BUG(bad masque server stream type)
          << "Unexpected stream type for stream ID "
          << request_handler->stream_id();
      return CreateBackendErrorResponse("500", "Bad stream type");
    }
    stream->RegisterHttp3DatagramFlowId(*flow_id);
    connect_udp_server_states_.push_back(
        ConnectUdpServerState(stream, context_id, target_server_address,
                              fd_wrapper.extract_fd(), this));

    // TODO(b/181256914) remove this when we drop support for
    // draft-ietf-masque-h3-datagram-00 in favor of later drafts.
    Http3DatagramContextExtensions extensions;
    stream->RegisterHttp3DatagramContextId(context_id, extensions,
                                           &connect_udp_server_states_.back());

    spdy::Http2HeaderBlock response_headers;
    response_headers[":status"] = "200";
    SpdyUtils::AddDatagramFlowIdHeader(&response_headers, *flow_id);
    auto response = std::make_unique<QuicBackendResponse>();
    response->set_response_type(QuicBackendResponse::INCOMPLETE_RESPONSE);
    response->set_headers(std::move(response_headers));
    response->set_body("");

    return response;
  }

  QUIC_DLOG(INFO) << "MasqueServerSession handling MASQUE request";

  if (masque_path == "init") {
    if (masque_initialized_) {
      QUIC_DLOG(ERROR) << "Got second MASQUE init request";
      return nullptr;
    }
    masque_initialized_ = true;
  } else if (masque_path == "unregister") {
    QuicConnectionId connection_id(request_body.data(), request_body.length());
    QUIC_DLOG(INFO) << "Received MASQUE request to unregister "
                    << connection_id;
    owner_->UnregisterClientConnectionId(connection_id);
    compression_engine_.UnregisterClientConnectionId(connection_id);
  } else {
    if (!masque_initialized_) {
      QUIC_DLOG(ERROR) << "Got MASQUE request before init";
      return nullptr;
    }
  }

  // TODO(dschinazi) implement binary protocol sent in response body.
  const std::string response_body = "";
  spdy::Http2HeaderBlock response_headers;
  response_headers[":status"] = "200";
  auto response = std::make_unique<QuicBackendResponse>();
  response->set_response_type(QuicBackendResponse::REGULAR_RESPONSE);
  response->set_headers(std::move(response_headers));
  response->set_body(response_body);

  return response;
}

void MasqueServerSession::HandlePacketFromServer(
    const ReceivedPacketInfo& packet_info) {
  QUIC_DVLOG(1) << "MasqueServerSession received " << packet_info;
  if (masque_mode_ == MasqueMode::kLegacy) {
    compression_engine_.CompressAndSendPacket(
        packet_info.packet.AsStringPiece(),
        packet_info.destination_connection_id, packet_info.source_connection_id,
        packet_info.peer_address);
    return;
  }
  QUIC_LOG(ERROR) << "Ignoring packet from server in " << masque_mode_
                  << " mode";
}

void MasqueServerSession::OnRegistration(QuicEpollServer* /*eps*/,
                                         QuicUdpSocketFd fd, int event_mask) {
  QUIC_DVLOG(1) << "OnRegistration " << fd << " event_mask " << event_mask;
}

void MasqueServerSession::OnModification(QuicUdpSocketFd fd, int event_mask) {
  QUIC_DVLOG(1) << "OnModification " << fd << " event_mask " << event_mask;
}

void MasqueServerSession::OnEvent(QuicUdpSocketFd fd, QuicEpollEvent* event) {
  if ((event->in_events & EPOLLIN) == 0) {
    QUIC_DVLOG(1) << "Ignoring OnEvent fd " << fd << " event mask "
                  << event->in_events;
    return;
  }
  auto it = absl::c_find_if(connect_udp_server_states_,
                            [fd](const ConnectUdpServerState& connect_udp) {
                              return connect_udp.fd() == fd;
                            });
  if (it == connect_udp_server_states_.end()) {
    QUIC_BUG(quic_bug_10974_1) << "Got unexpected event mask "
                               << event->in_events << " on unknown fd " << fd;
    return;
  }
  QuicSocketAddress expected_target_server_address =
      it->target_server_address();
  QUICHE_DCHECK(expected_target_server_address.IsInitialized());
  QUIC_DVLOG(1) << "Received readable event on fd " << fd << " (mask "
                << event->in_events << ") stream ID " << it->stream()->id()
                << " server " << expected_target_server_address;
  QuicUdpSocketApi socket_api;
  BitMask64 packet_info_interested(QuicUdpPacketInfoBit::PEER_ADDRESS);
  char packet_buffer[kMaxIncomingPacketSize];
  char control_buffer[kDefaultUdpPacketControlBufferSize];
  while (true) {
    QuicUdpSocketApi::ReadPacketResult read_result;
    read_result.packet_buffer = {packet_buffer, sizeof(packet_buffer)};
    read_result.control_buffer = {control_buffer, sizeof(control_buffer)};
    socket_api.ReadPacket(fd, packet_info_interested, &read_result);
    if (!read_result.ok) {
      // Most likely there is nothing left to read, break out of read loop.
      break;
    }
    if (!read_result.packet_info.HasValue(QuicUdpPacketInfoBit::PEER_ADDRESS)) {
      QUIC_BUG(quic_bug_10974_2)
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
      QUIC_BUG(quic_bug_10974_3)
          << "Unexpected incoming UDP packet on fd " << fd << " from "
          << expected_target_server_address
          << " because MASQUE connection is closed";
      return;
    }
    // The packet is valid, send it to the client in a DATAGRAM frame.
    MessageStatus message_status = it->stream()->SendHttp3Datagram(
        it->context_id(),
        absl::string_view(read_result.packet_buffer.buffer,
                          read_result.packet_buffer.buffer_len));
    QUIC_DVLOG(1) << "Sent UDP packet from " << expected_target_server_address
                  << " of length " << read_result.packet_buffer.buffer_len
                  << " with stream ID " << it->stream()->id()
                  << " and got message status "
                  << MessageStatusToString(message_status);
  }
}

void MasqueServerSession::OnUnregistration(QuicUdpSocketFd fd, bool replaced) {
  QUIC_DVLOG(1) << "OnUnregistration " << fd << " " << (replaced ? "" : "!")
                << " replaced";
}

void MasqueServerSession::OnShutdown(QuicEpollServer* /*eps*/,
                                     QuicUdpSocketFd fd) {
  QUIC_DVLOG(1) << "OnShutdown " << fd;
}

std::string MasqueServerSession::Name() const {
  return std::string("MasqueServerSession-") + connection_id().ToString();
}

MasqueServerSession::ConnectUdpServerState::ConnectUdpServerState(
    QuicSpdyStream* stream, absl::optional<QuicDatagramContextId> context_id,
    const QuicSocketAddress& target_server_address, QuicUdpSocketFd fd,
    MasqueServerSession* masque_session)
    : stream_(stream),
      context_id_(context_id),
      target_server_address_(target_server_address),
      fd_(fd),
      masque_session_(masque_session) {
  QUICHE_DCHECK_NE(fd_, kQuicInvalidSocketFd);
  QUICHE_DCHECK_NE(masque_session_, nullptr);
  this->stream()->RegisterHttp3DatagramRegistrationVisitor(this);
}

MasqueServerSession::ConnectUdpServerState::~ConnectUdpServerState() {
  if (stream() != nullptr) {
    stream()->UnregisterHttp3DatagramRegistrationVisitor();
    if (context_registered_) {
      stream()->UnregisterHttp3DatagramContextId(context_id());
    }
  }
  if (fd_ == kQuicInvalidSocketFd) {
    return;
  }
  QuicUdpSocketApi socket_api;
  QUIC_DLOG(INFO) << "Closing fd " << fd_;
  masque_session_->epoll_server()->UnregisterFD(fd_);
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
    masque_session_->epoll_server()->UnregisterFD(fd_);
    socket_api.Destroy(fd_);
  }
  stream_ = other.stream_;
  other.stream_ = nullptr;
  context_id_ = other.context_id_;
  target_server_address_ = other.target_server_address_;
  fd_ = other.fd_;
  masque_session_ = other.masque_session_;
  other.fd_ = kQuicInvalidSocketFd;
  context_registered_ = other.context_registered_;
  other.context_registered_ = false;
  if (stream() != nullptr) {
    stream()->MoveHttp3DatagramRegistration(this);
    if (context_registered_) {
      stream()->MoveHttp3DatagramContextIdRegistration(context_id(), this);
    }
  }
  return *this;
}

void MasqueServerSession::ConnectUdpServerState::OnHttp3Datagram(
    QuicStreamId stream_id, absl::optional<QuicDatagramContextId> context_id,
    absl::string_view payload) {
  QUICHE_DCHECK_EQ(stream_id, stream()->id());
  QUICHE_DCHECK(context_id == context_id_);
  QuicUdpSocketApi socket_api;
  QuicUdpPacketInfo packet_info;
  packet_info.SetPeerAddress(target_server_address_);
  WriteResult write_result = socket_api.WritePacket(
      fd_, payload.data(), payload.length(), packet_info);
  QUIC_DVLOG(1) << "Wrote packet of length " << payload.length() << " to "
                << target_server_address_ << " with result " << write_result;
}

void MasqueServerSession::ConnectUdpServerState::OnContextReceived(
    QuicStreamId stream_id, absl::optional<QuicDatagramContextId> context_id,
    const Http3DatagramContextExtensions& /*extensions*/) {
  if (stream_id != stream()->id()) {
    QUIC_BUG(MASQUE server bad datagram context registration)
        << "Registered stream ID " << stream_id << ", expected "
        << stream()->id();
    return;
  }
  if (!context_received_) {
    context_received_ = true;
    context_id_ = context_id;
  }
  if (context_id != context_id_) {
    QUIC_DLOG(INFO) << "Ignoring unexpected context ID "
                    << (context_id.has_value() ? context_id.value() : 0)
                    << " instead of "
                    << (context_id_.has_value() ? context_id_.value() : 0)
                    << " on stream ID " << stream()->id();
    return;
  }
  if (context_registered_) {
    QUIC_BUG(MASQUE server double datagram context registration)
        << "Try to re-register stream ID " << stream_id << " context ID "
        << (context_id_.has_value() ? context_id_.value() : 0);
    return;
  }
  context_registered_ = true;
  Http3DatagramContextExtensions reply_extensions;
  stream()->RegisterHttp3DatagramContextId(context_id_, reply_extensions, this);
}

void MasqueServerSession::ConnectUdpServerState::OnContextClosed(
    QuicStreamId stream_id, absl::optional<QuicDatagramContextId> context_id,
    const Http3DatagramContextExtensions& /*extensions*/) {
  if (stream_id != stream()->id()) {
    QUIC_BUG(MASQUE server bad datagram context registration)
        << "Closed context on stream ID " << stream_id << ", expected "
        << stream()->id();
    return;
  }
  if (context_id != context_id_) {
    QUIC_DLOG(INFO) << "Ignoring unexpected close of context ID "
                    << (context_id.has_value() ? context_id.value() : 0)
                    << " instead of "
                    << (context_id_.has_value() ? context_id_.value() : 0)
                    << " on stream ID " << stream()->id();
    return;
  }
  QUIC_DLOG(INFO) << "Received datagram context close on stream ID "
                  << stream()->id() << ", closing stream";
  masque_session_->ResetStream(stream()->id(), QUIC_STREAM_CANCELLED);
}

}  // namespace quic
