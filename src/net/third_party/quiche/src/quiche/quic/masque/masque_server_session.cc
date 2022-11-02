// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_server_session.h"

#include <netdb.h>

#include <cstddef>
#include <limits>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/quic/core/http/spdy_utils.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_udp_socket.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/platform/api/quiche_url_utils.h"

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
  // Artificially increase the max packet length to 1350 to ensure we can fit
  // QUIC packets inside DATAGRAM frames.
  // TODO(b/181606597) Remove this workaround once we use PMTUD.
  connection->SetMaxPacketLength(kDefaultMaxPacketSize);

  masque_server_backend_->RegisterBackendClient(connection_id(), this);
  QUICHE_DCHECK_NE(event_loop_, nullptr);
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
    const spdy::Http2HeaderBlock& request_headers,
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  auto path_pair = request_headers.find(":path");
  auto scheme_pair = request_headers.find(":scheme");
  auto method_pair = request_headers.find(":method");
  auto protocol_pair = request_headers.find(":protocol");
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
  if (protocol_pair == request_headers.end()) {
    QUIC_DLOG(ERROR) << "MASQUE request is missing :protocol";
    return CreateBackendErrorResponse("400", "Missing :protocol");
  }
  if (authority_pair == request_headers.end()) {
    QUIC_DLOG(ERROR) << "MASQUE request is missing :authority";
    return CreateBackendErrorResponse("400", "Missing :authority");
  }
  absl::string_view path = path_pair->second;
  absl::string_view scheme = scheme_pair->second;
  absl::string_view method = method_pair->second;
  absl::string_view protocol = protocol_pair->second;
  absl::string_view authority = authority_pair->second;
  if (path.empty()) {
    QUIC_DLOG(ERROR) << "MASQUE request with empty path";
    return CreateBackendErrorResponse("400", "Empty path");
  }
  if (scheme.empty()) {
    return CreateBackendErrorResponse("400", "Empty scheme");
  }
  if (method != "CONNECT") {
    QUIC_DLOG(ERROR) << "MASQUE request with bad method \"" << method << "\"";
    return CreateBackendErrorResponse("400", "Bad method");
  }
  if (protocol != "connect-udp") {
    QUIC_DLOG(ERROR) << "MASQUE request with bad protocol \"" << protocol
                     << "\"";
    return CreateBackendErrorResponse("400", "Bad protocol");
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
  absl::optional<std::string> host = quiche::AsciiUrlDecode(path_split[4]);
  if (!host.has_value()) {
    QUIC_DLOG(ERROR) << "Failed to decode host \"" << path_split[4] << "\"";
    return CreateBackendErrorResponse("500", "Failed to decode host");
  }
  absl::optional<std::string> port = quiche::AsciiUrlDecode(path_split[5]);
  if (!port.has_value()) {
    QUIC_DLOG(ERROR) << "Failed to decode port \"" << path_split[5] << "\"";
    return CreateBackendErrorResponse("500", "Failed to decode port");
  }

  // Perform DNS resolution.
  addrinfo hint = {};
  hint.ai_protocol = IPPROTO_UDP;

  addrinfo* info_list = nullptr;
  int result = getaddrinfo(host.value().c_str(), port.value().c_str(), &hint,
                           &info_list);
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
  auto it = absl::c_find_if(connect_udp_server_states_,
                            [fd](const ConnectUdpServerState& connect_udp) {
                              return connect_udp.fd() == fd;
                            });
  if (it == connect_udp_server_states_.end()) {
    QUIC_BUG(quic_bug_10974_1)
        << "Got unexpected event mask " << events << " on unknown fd " << fd;
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

  QuicSocketAddress expected_target_server_address =
      it->target_server_address();
  QUICHE_DCHECK(expected_target_server_address.IsInitialized());
  QUIC_DVLOG(1) << "Received readable event on fd " << fd << " (mask " << events
                << ") stream ID " << it->stream()->id() << " server "
                << expected_target_server_address;
  QuicUdpSocketApi socket_api;
  BitMask64 packet_info_interested(QuicUdpPacketInfoBit::PEER_ADDRESS);
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
    MessageStatus message_status =
        it->stream()->SendHttp3Datagram(absl::string_view(
            packet_buffer, read_result.packet_buffer.buffer_len + 1));
    QUIC_DVLOG(1) << "Sent UDP packet from " << expected_target_server_address
                  << " of length " << read_result.packet_buffer.buffer_len
                  << " with stream ID " << it->stream()->id()
                  << " and got message status "
                  << MessageStatusToString(message_status);
  }
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

}  // namespace quic
