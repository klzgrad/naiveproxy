// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_client_session.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "openssl/curve25519.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/frames/quic_connection_close_frame.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/capsule.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_googleurl.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_url_utils.h"
#include "quiche/common/quiche_ip_address.h"
#include "quiche/common/quiche_random.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {

namespace {

using ::quiche::AddressAssignCapsule;
using ::quiche::AddressRequestCapsule;
using ::quiche::RouteAdvertisementCapsule;
using ContextId = MasqueClientSession::ContextId;

constexpr MasqueClientSession::ContextId kConnectIpPayloadContextId = 0;
constexpr MasqueClientSession::ContextId kConnectEthernetPayloadContextId = 0;
}  // namespace

MasqueClientSession::MasqueClientSession(
    MasqueMode masque_mode, const std::string& uri_template,
    const QuicConfig& config, const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection, const QuicServerId& server_id,
    QuicCryptoClientConfig* crypto_config, Owner* owner)
    : QuicSpdyClientSession(config, supported_versions, connection, server_id,
                            crypto_config),
      masque_mode_(masque_mode),
      uri_template_(uri_template),
      owner_(owner) {
  QUICHE_CHECK(!QuicUrl(uri_template_).host().empty());
  // We don't currently use `masque_mode_` but will in the future. To silence
  // clang's `-Wunused-private-field` warning for this when building QUICHE for
  // Chrome, add a use of it here.
  (void)masque_mode_;
}

MasqueClientSession::MasqueClientSession(
    const QuicConfig& config, const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection, const QuicServerId& server_id,
    QuicCryptoClientConfig* crypto_config, Owner* owner)
    : QuicSpdyClientSession(config, supported_versions, connection, server_id,
                            crypto_config),
      owner_(owner) {}

void MasqueClientSession::OnDatagramAcked(QuicDatagramId datagram_id,
                                          QuicTime /*receive_timestamp*/) {
  QUIC_DVLOG(1) << "Received ack for DATAGRAM frame " << datagram_id;
}

void MasqueClientSession::OnDatagramLost(QuicDatagramId datagram_id) {
  QUIC_DVLOG(1) << "We believe DATAGRAM frame " << datagram_id << " was lost";
}

MasqueClientSession::ConnectUdpClientState*
MasqueClientSession::GetOrCreateConnectUdpClientState(
    const QuicSocketAddress& target_server_address,
    EncapsulatedClientSession* encapsulated_client_session) {
  for (ConnectUdpClientState& client_state : connect_udp_client_states_) {
    if (client_state.target_server_address() == target_server_address &&
        client_state.encapsulated_client_session() ==
            encapsulated_client_session &&
        IsBind() == client_state.is_bind()) {
      // Found existing CONNECT-UDP request.
      return &client_state;
    }
  }

  QUIC_DLOG(INFO) << "No existing state non bind state found, create one";
  // No CONNECT-UDP request found, create a new one.
  std::string target_host, target_port;

  if (IsBind()) {
    target_host = "*";
    target_port = "*";
  } else {
    auto it =
        fake_addresses_.find(target_server_address.host().ToPackedString());
    if (it != fake_addresses_.end()) {
      target_host = it->second;
    } else {
      target_host = target_server_address.host().ToString();
    }
    target_port = absl::StrCat(target_server_address.port());
  }
  QUICHE_CHECK(!target_host.empty());

  url::Parsed parsed_uri_template;
  url::ParseStandardURL(uri_template_.c_str(), uri_template_.length(),
                        &parsed_uri_template);
  if (!parsed_uri_template.path.is_nonempty()) {
    QUIC_BUG(bad URI template path)
        << connection_id() << ": Cannot parse path from URI template \""
        << uri_template_ << "\"";
    return nullptr;
  }
  std::string path = uri_template_.substr(parsed_uri_template.path.begin,
                                          parsed_uri_template.path.len);
  if (parsed_uri_template.query.is_valid()) {
    absl::StrAppend(&path, "?",
                    uri_template_.substr(parsed_uri_template.query.begin,
                                         parsed_uri_template.query.len));
  }
  absl::flat_hash_map<std::string, std::string> parameters;
  // TODO(abhisinghx): Remove this hack.
  // ExpandURITemplate, does not encode * as %2A.
  // Therefore to meet the spec, we need to do it manually.
  if (IsBind()) {
    parameters["target_host"] = "HackyStringForBind";
    parameters["target_port"] = "HackyStringForBind";
  } else {
    parameters["target_host"] = target_host;
    parameters["target_port"] = target_port;
  }

  std::string expanded_path;
  absl::flat_hash_set<std::string> vars_found;
  bool expanded =
      quiche::ExpandURITemplate(path, parameters, &expanded_path, &vars_found);
  if (!expanded || vars_found.find("target_host") == vars_found.end() ||
      vars_found.find("target_port") == vars_found.end()) {
    QUIC_DLOG(ERROR) << "Failed to expand URI template \"" << uri_template_
                     << "\" for " << target_host << " port "
                     << target_server_address.port();
    return nullptr;
  }
  if (IsBind()) {
    expanded_path =
        absl::StrReplaceAll(expanded_path, {{"HackyStringForBind", "%2A"}});
  }

  url::Component expanded_path_component(0, expanded_path.length());
  url::RawCanonOutput<1024> canonicalized_path_output;
  url::Component canonicalized_path_component;
  bool canonicalized = url::CanonicalizePath(
      expanded_path.c_str(), expanded_path_component,
      &canonicalized_path_output, &canonicalized_path_component);
  if (!canonicalized || !canonicalized_path_component.is_nonempty()) {
    QUIC_DLOG(ERROR) << "Failed to canonicalize URI template \""
                     << uri_template_ << "\" for " << target_host << " port "
                     << target_server_address.port();
    return nullptr;
  }
  std::string canonicalized_path(
      canonicalized_path_output.data() + canonicalized_path_component.begin,
      canonicalized_path_component.len);

  QuicSpdyClientStream* stream = CreateOutgoingBidirectionalStream();
  if (stream == nullptr) {
    // Stream flow control limits prevented us from opening a new stream.
    QUIC_DLOG(ERROR) << "Failed to open CONNECT-UDP stream";
    return nullptr;
  }

  QuicUrl url(uri_template_);
  std::string scheme = url.scheme();
  std::string authority = url.HostPort();

  QUIC_DLOG(INFO) << "Sending CONNECT-UDP request for " << target_host
                  << " port " << target_server_address.port() << " on stream "
                  << stream->id() << " scheme=\"" << scheme << "\" authority=\""
                  << authority << "\" path=\"" << canonicalized_path << "\"";

  // Send the request.
  quiche::HttpHeaderBlock headers;
  headers[":method"] = "CONNECT";
  headers[":protocol"] = "connect-udp";
  headers[":scheme"] = scheme;
  headers[":authority"] = authority;
  headers[":path"] = canonicalized_path;
  if (IsBind()) {
    headers["connect-udp-bind"] = "?1";
  }
  AddAdditionalHeaders(headers, url);
  QUIC_DVLOG(1) << "Sending request headers: " << headers.DebugString();
  size_t bytes_sent =
      stream->SendRequest(std::move(headers), /*body=*/"", /*fin=*/false);
  if (bytes_sent == 0) {
    QUIC_DLOG(ERROR) << "Failed to send CONNECT-UDP request";
    return nullptr;
  }

  connect_udp_client_states_.push_back(ConnectUdpClientState(
      stream, encapsulated_client_session, this, target_server_address));
  if (IsBind()) {
    QUIC_DLOG(INFO) << "Creating bind state";
    connect_udp_client_states_.back().set_is_bind(true);
  }
  return &connect_udp_client_states_.back();
}

const MasqueClientSession::ConnectIpClientState*
MasqueClientSession::GetOrCreateConnectIpClientState(
    MasqueClientSession::EncapsulatedIpSession* encapsulated_ip_session) {
  for (const ConnectIpClientState& client_state : connect_ip_client_states_) {
    if (client_state.encapsulated_ip_session() == encapsulated_ip_session) {
      // Found existing CONNECT-IP request.
      return &client_state;
    }
  }
  // No CONNECT-IP request found, create a new one.
  QuicSpdyClientStream* stream = CreateOutgoingBidirectionalStream();
  if (stream == nullptr) {
    // Stream flow control limits prevented us from opening a new stream.
    QUIC_DLOG(ERROR) << "Failed to open CONNECT-IP stream";
    return nullptr;
  }

  QuicUrl url(uri_template_);
  std::string scheme = url.scheme();
  std::string authority = url.HostPort();
  std::string path = "/.well-known/masque/ip/*/*/";

  QUIC_DLOG(INFO) << "Sending CONNECT-IP request on stream " << stream->id()
                  << " scheme=\"" << scheme << "\" authority=\"" << authority
                  << "\" path=\"" << path << "\"";

  // Send the request.
  quiche::HttpHeaderBlock headers;
  headers[":method"] = "CONNECT";
  headers[":protocol"] = "connect-ip";
  headers[":scheme"] = scheme;
  headers[":authority"] = authority;
  headers[":path"] = path;
  headers["connect-ip-version"] = "3";
  AddAdditionalHeaders(headers, url);
  QUIC_DVLOG(1) << "Sending request headers: " << headers.DebugString();
  size_t bytes_sent =
      stream->SendRequest(std::move(headers), /*body=*/"", /*fin=*/false);
  if (bytes_sent == 0) {
    QUIC_DLOG(ERROR) << "Failed to send CONNECT-IP request";
    return nullptr;
  }

  connect_ip_client_states_.push_back(
      ConnectIpClientState(stream, encapsulated_ip_session, this));
  return &connect_ip_client_states_.back();
}

const MasqueClientSession::ConnectEthernetClientState*
MasqueClientSession::GetOrCreateConnectEthernetClientState(
    MasqueClientSession::EncapsulatedEthernetSession*
        encapsulated_ethernet_session) {
  for (const ConnectEthernetClientState& client_state :
       connect_ethernet_client_states_) {
    if (client_state.encapsulated_ethernet_session() ==
        encapsulated_ethernet_session) {
      // Found existing CONNECT-ETHERNET request.
      return &client_state;
    }
  }
  // No CONNECT-ETHERNET request found, create a new one.
  QuicSpdyClientStream* stream = CreateOutgoingBidirectionalStream();
  if (stream == nullptr) {
    // Stream flow control limits prevented us from opening a new stream.
    QUIC_DLOG(ERROR) << "Failed to open CONNECT-ETHERNET stream";
    return nullptr;
  }

  QuicUrl url(uri_template_);
  std::string scheme = url.scheme();
  std::string authority = url.HostPort();
  std::string path = "/.well-known/masque/ethernet/";

  QUIC_DLOG(INFO) << "Sending CONNECT-ETHERNET request on stream "
                  << stream->id() << " scheme=\"" << scheme << "\" authority=\""
                  << authority << "\" path=\"" << path << "\"";

  // Send the request.
  quiche::HttpHeaderBlock headers;
  headers[":method"] = "CONNECT";
  headers[":protocol"] = "connect-ethernet";
  headers[":scheme"] = scheme;
  headers[":authority"] = authority;
  headers[":path"] = path;
  AddAdditionalHeaders(headers, url);
  QUIC_DVLOG(1) << "Sending request headers: " << headers.DebugString();
  size_t bytes_sent =
      stream->SendRequest(std::move(headers), /*body=*/"", /*fin=*/false);
  if (bytes_sent == 0) {
    QUIC_DLOG(ERROR) << "Failed to send CONNECT-ETHERNET request";
    return nullptr;
  }

  connect_ethernet_client_states_.push_back(
      ConnectEthernetClientState(stream, encapsulated_ethernet_session, this));
  return &connect_ethernet_client_states_.back();
}

void MasqueClientSession::SendIpPacket(
    absl::string_view packet,
    MasqueClientSession::EncapsulatedIpSession* encapsulated_ip_session) {
  const ConnectIpClientState* connect_ip =
      GetOrCreateConnectIpClientState(encapsulated_ip_session);
  if (connect_ip == nullptr) {
    QUIC_DLOG(ERROR) << "Failed to create CONNECT-IP request";
    return;
  }

  std::string http_payload;
  http_payload.resize(
      QuicDataWriter::GetVarInt62Len(kConnectIpPayloadContextId) +
      packet.size());
  QuicDataWriter writer(http_payload.size(), http_payload.data());
  if (!writer.WriteVarInt62(kConnectIpPayloadContextId)) {
    QUIC_BUG(IP context write fail) << "Failed to write CONNECT-IP context ID";
    return;
  }
  if (!writer.WriteStringPiece(packet)) {
    QUIC_BUG(IP packet write fail) << "Failed to write CONNECT-IP packet";
    return;
  }
  DatagramStatus message_status =
      SendHttp3Datagram(connect_ip->stream()->id(), http_payload);

  QUIC_DVLOG(1) << "Sent encapsulated IP packet of length " << packet.size()
                << " with stream ID " << connect_ip->stream()->id()
                << " and got message status "
                << DatagramStatusToString(message_status);
}

void MasqueClientSession::SendEthernetFrame(
    absl::string_view frame, MasqueClientSession::EncapsulatedEthernetSession*
                                 encapsulated_ethernet_session) {
  const ConnectEthernetClientState* connect_ethernet =
      GetOrCreateConnectEthernetClientState(encapsulated_ethernet_session);
  if (connect_ethernet == nullptr) {
    QUIC_DLOG(ERROR) << "Failed to create CONNECT-ETHERNET request";
    return;
  }

  std::string http_payload;
  http_payload.resize(
      QuicDataWriter::GetVarInt62Len(kConnectEthernetPayloadContextId) +
      frame.size());
  QuicDataWriter writer(http_payload.size(), http_payload.data());
  if (!writer.WriteVarInt62(kConnectEthernetPayloadContextId)) {
    QUIC_BUG(IP context write fail)
        << "Failed to write CONNECT-ETHERNET context ID";
    return;
  }
  if (!writer.WriteStringPiece(frame)) {
    QUIC_BUG(IP packet write fail) << "Failed to write CONNECT-ETHERNET frame";
    return;
  }
  DatagramStatus message_status =
      SendHttp3Datagram(connect_ethernet->stream()->id(), http_payload);

  QUIC_DVLOG(1) << "Sent encapsulated Ethernet frame of length " << frame.size()
                << " with stream ID " << connect_ethernet->stream()->id()
                << " and got message status "
                << DatagramStatusToString(message_status);
}

void MasqueClientSession::SendConnectUdpBindPacket(
    absl::string_view packet, const QuicSocketAddress& target_server_address,
    EncapsulatedClientSession* encapsulated_client_session) {
  QUICHE_DCHECK(masque_mode_ == MasqueMode::kConnectUdpBind);
  ConnectUdpClientState* connect_udp = GetOrCreateConnectUdpClientState(
      QuicSocketAddress(), encapsulated_client_session);
  std::optional<std::string> bind_packet = connect_udp->PrepareBindPacket(
      packet, target_server_address, bind_use_uncompressed_context_);
  if (!bind_packet.has_value()) {
    QUIC_DLOG(ERROR) << "Failed to prepare bind packet";
    return;
  }
  QUIC_DVLOG(1) << "Sending bind packet: " << bind_packet.value()
                << " of length " << bind_packet.value().length() << " to "
                << target_server_address;

  SendHttp3Datagram(connect_udp->stream()->id(), bind_packet.value());
}

void MasqueClientSession::SendPacket(
    absl::string_view packet, const QuicSocketAddress& target_server_address,
    EncapsulatedClientSession* encapsulated_client_session) {
  const ConnectUdpClientState* connect_udp = GetOrCreateConnectUdpClientState(
      target_server_address, encapsulated_client_session);
  if (connect_udp == nullptr) {
    QUIC_DLOG(ERROR) << "Failed to create CONNECT-UDP request";
    return;
  }
  if (masque_mode_ == MasqueMode::kConnectUdpBind) {
    QUIC_LOG(ERROR) << "Should not be called in CONNECT-UDP Bind mode";
    return;
  }

  std::string http_payload;
  http_payload.resize(1 + packet.size());
  http_payload[0] = 0;
  memcpy(&http_payload[1], packet.data(), packet.size());
  DatagramStatus message_status =
      SendHttp3Datagram(connect_udp->stream()->id(), http_payload);

  QUIC_DVLOG(1) << "Sent packet to " << target_server_address
                << " compressed with stream ID " << connect_udp->stream()->id()
                << " and got message status "
                << DatagramStatusToString(message_status);
  QUIC_DVLOG(2) << "Contents of outgoing HTTP Datagram of length "
                << http_payload.size() << ":" << std::endl
                << quiche::QuicheTextUtils::HexDump(http_payload);
}

// Send Capsule for CONNECT-UDP Bind.
void MasqueClientSession::SendBindCapsule(
    const quiche::Capsule& capsule,
    EncapsulatedClientSession* encapsulated_client_session) {
  for (auto& state : connect_udp_client_states_) {
    if (state.is_bind() &&
        state.encapsulated_client_session() == encapsulated_client_session) {
      state.stream()->WriteCapsule(capsule);
    }
  }
}

void MasqueClientSession::CloseConnectUdpStream(
    EncapsulatedClientSession* encapsulated_client_session) {
  for (auto it = connect_udp_client_states_.begin();
       it != connect_udp_client_states_.end();) {
    if (it->encapsulated_client_session() == encapsulated_client_session) {
      QUIC_DLOG(INFO) << "Removing CONNECT-UDP state for stream ID "
                      << it->stream()->id();
      auto* stream = it->stream();
      it = connect_udp_client_states_.erase(it);
      if (!stream->write_side_closed()) {
        stream->Reset(QUIC_STREAM_CANCELLED);
      }
    } else {
      ++it;
    }
  }
}

void MasqueClientSession::CloseConnectIpStream(
    EncapsulatedIpSession* encapsulated_ip_session) {
  for (auto it = connect_ip_client_states_.begin();
       it != connect_ip_client_states_.end();) {
    if (it->encapsulated_ip_session() == encapsulated_ip_session) {
      QUIC_DLOG(INFO) << "Removing CONNECT-IP state for stream ID "
                      << it->stream()->id();
      auto* stream = it->stream();
      it = connect_ip_client_states_.erase(it);
      if (!stream->write_side_closed()) {
        stream->Reset(QUIC_STREAM_CANCELLED);
      }
    } else {
      ++it;
    }
  }
}

void MasqueClientSession::CloseConnectEthernetStream(
    EncapsulatedEthernetSession* encapsulated_ethernet_session) {
  for (auto it = connect_ethernet_client_states_.begin();
       it != connect_ethernet_client_states_.end();) {
    if (it->encapsulated_ethernet_session() == encapsulated_ethernet_session) {
      QUIC_DLOG(INFO) << "Removing CONNECT-ETHERNET state for stream ID "
                      << it->stream()->id();
      auto* stream = it->stream();
      it = connect_ethernet_client_states_.erase(it);
      if (!stream->write_side_closed()) {
        stream->Reset(QUIC_STREAM_CANCELLED);
      }
    } else {
      ++it;
    }
  }
}

void MasqueClientSession::OnConnectionClosed(
    const QuicConnectionCloseFrame& frame, ConnectionCloseSource source) {
  QuicSpdyClientSession::OnConnectionClosed(frame, source);
  // Close all encapsulated sessions.
  for (const auto& client_state : connect_udp_client_states_) {
    client_state.encapsulated_client_session()->CloseConnection(
        QUIC_CONNECTION_CANCELLED, "Underlying MASQUE connection was closed",
        ConnectionCloseBehavior::SILENT_CLOSE);
  }
  for (const auto& client_state : connect_ip_client_states_) {
    client_state.encapsulated_ip_session()->CloseIpSession(
        "Underlying MASQUE connection was closed");
  }
}

void MasqueClientSession::OnStreamClosed(QuicStreamId stream_id) {
  if (QuicUtils::IsBidirectionalStreamId(stream_id, version()) &&
      QuicUtils::IsClientInitiatedStreamId(transport_version(), stream_id)) {
    QuicSpdyClientStream* stream =
        reinterpret_cast<QuicSpdyClientStream*>(GetActiveStream(stream_id));
    if (stream != nullptr) {
      QUIC_DLOG(INFO) << "Stream " << stream_id
                      << " closed, got response headers:"
                      << stream->response_headers().DebugString();
    }
  }
  for (auto it = connect_udp_client_states_.begin();
       it != connect_udp_client_states_.end();) {
    if (it->stream()->id() == stream_id) {
      QUIC_DLOG(INFO) << "Stream " << stream_id
                      << " was closed, removing CONNECT-UDP state";
      auto* encapsulated_client_session = it->encapsulated_client_session();
      it = connect_udp_client_states_.erase(it);
      encapsulated_client_session->CloseConnection(
          QUIC_CONNECTION_CANCELLED,
          "Underlying MASQUE CONNECT-UDP stream was closed",
          ConnectionCloseBehavior::SILENT_CLOSE);
    } else {
      ++it;
    }
  }
  for (auto it = connect_ip_client_states_.begin();
       it != connect_ip_client_states_.end();) {
    if (it->stream()->id() == stream_id) {
      QUIC_DLOG(INFO) << "Stream " << stream_id
                      << " was closed, removing CONNECT-IP state";
      auto* encapsulated_ip_session = it->encapsulated_ip_session();
      it = connect_ip_client_states_.erase(it);
      encapsulated_ip_session->CloseIpSession(
          "Underlying MASQUE CONNECT-IP stream was closed");
    } else {
      ++it;
    }
  }

  QuicSpdyClientSession::OnStreamClosed(stream_id);
}

bool MasqueClientSession::OnSettingsFrame(const SettingsFrame& frame) {
  QUIC_DLOG(INFO) << connection_id() << " Received SETTINGS: " << frame;
  if (!QuicSpdyClientSession::OnSettingsFrame(frame)) {
    QUIC_DLOG(ERROR) << "Failed to parse received settings";
    return false;
  }
  if (!SupportsH3Datagram()) {
    QUIC_DLOG(ERROR) << "Warning: MasqueClientSession without HTTP/3 Datagrams";
  }
  QUIC_DLOG(INFO) << "Using HTTP Datagram: " << http_datagram_support();
  owner_->OnSettingsReceived();
  return true;
}

MasqueClientSession::ConnectUdpClientState::ConnectUdpClientState(
    QuicSpdyClientStream* stream,
    EncapsulatedClientSession* encapsulated_client_session,
    MasqueClientSession* masque_session,
    const QuicSocketAddress& target_server_address)
    : stream_(stream),
      encapsulated_client_session_(encapsulated_client_session),
      masque_session_(masque_session),
      target_server_address_(target_server_address) {
  QUICHE_DCHECK_NE(masque_session_, nullptr);
  this->stream()->RegisterHttp3DatagramVisitor(this);
  if (masque_session->IsBind()) {
    this->stream()->RegisterConnectUdpBindVisitor(this);
  }
}

MasqueClientSession::ConnectUdpClientState::~ConnectUdpClientState() {
  if (stream() != nullptr) {
    stream()->UnregisterHttp3DatagramVisitor();
    if (masque_session_->IsBind()) {
      this->stream()->UnregisterConnectUdpBindVisitor();
    }
  }
}

MasqueClientSession::ConnectUdpClientState::ConnectUdpClientState(
    MasqueClientSession::ConnectUdpClientState&& other) {
  *this = std::move(other);
}

MasqueClientSession::ConnectUdpClientState&
MasqueClientSession::ConnectUdpClientState::operator=(
    MasqueClientSession::ConnectUdpClientState&& other) {
  stream_ = other.stream_;
  encapsulated_client_session_ = other.encapsulated_client_session_;
  masque_session_ = other.masque_session_;
  target_server_address_ = other.target_server_address_;
  other.stream_ = nullptr;
  other.is_bind_ = is_bind_;
  other.bind_context_ip_map_ = std::move(bind_context_ip_map_);
  if (stream() != nullptr) {
    stream()->ReplaceHttp3DatagramVisitor(this);
    if (masque_session_->IsBind()) {
      stream()->ReplaceConnectUdpBindVisitor(this);
    }
  }
  return *this;
}

void MasqueClientSession::ConnectUdpClientState::OnHttp3Datagram(
    QuicStreamId stream_id, absl::string_view payload) {
  QUIC_DVLOG(1) << "Received HTTP Datagram of length " << payload.size();
  QUICHE_DCHECK_EQ(stream_id, stream()->id());
  QuicDataReader reader(payload);
  if (!is_bind_) {
    ContextId context_id;
    if (!reader.ReadVarInt62(&context_id)) {
      QUIC_DLOG(ERROR) << "Failed to read context ID";
      return;
    }
    if (context_id != 0) {
      QUIC_DLOG(ERROR) << "Ignoring HTTP Datagram with unexpected context ID "
                       << context_id;
      return;
    }
  } else {
    ContextId context_id;
    QuicSocketAddress target_address;
    if (!reader.ReadVarInt62(&context_id)) {
      QUIC_DLOG(ERROR) << "Failed to read context ID";
      return;
    }
    if (!bind_context_ip_map_.contains(context_id)) {
      // Non existent context id.
      QUIC_DLOG(ERROR) << "Ignoring HTTP Datagram with non existent context id "
                       << context_id;
      // Drop the packet.
      return;
    }
    if (bind_context_ip_map_[context_id] == quiche::QuicheSocketAddress()) {
      // Uncompressed Context.
      // Parse IP and port.
      uint8_t ip_version;
      if (!reader.ReadUInt8(&ip_version)) {
        QUIC_DLOG(ERROR) << "Failed to read IP version";
        return;
      }
      auto address_size = ip_version == 4
                              ? quiche::QuicheIpAddress::kIPv4AddressSize
                              : quiche::QuicheIpAddress::kIPv6AddressSize;
      absl::string_view ip_address_bytes;
      if (!reader.ReadStringPiece(&ip_address_bytes, address_size)) {
        QUIC_DLOG(ERROR) << "Failed to read IP address";
        return;
      }
      quiche::QuicheIpAddress ip_address;
      if (!ip_address.FromPackedString(ip_address_bytes.data(), address_size)) {
        QUIC_DLOG(ERROR) << "Failed to parse IP address";
        return;
      }
      uint16_t port;
      if (!reader.ReadUInt16(&port)) {
        QUIC_DLOG(ERROR) << "Failed to read port";
        return;
      }
      target_address = QuicSocketAddress(ip_address, port);
    } else {
      target_address = bind_context_ip_map_[context_id];
    }
  }

  absl::string_view http_payload = reader.ReadRemainingPayload();
  encapsulated_client_session_->ProcessPacket(http_payload,
                                              target_server_address_);
  QUIC_DVLOG(1) << "Sent " << http_payload.size()
                << " bytes to connection for stream ID " << stream_id;
}

ContextId MasqueClientSession::ConnectUdpClientState::CreateCompressedContext(
    const QuicSocketAddress& target_address) {
  bind_context_ip_map_[next_available_context_id_] = target_address;
  quiche::Capsule capsule = quiche::Capsule::CompressionAssign();
  capsule.compression_assign_capsule().context_id = next_available_context_id_;
  capsule.compression_assign_capsule().ip_address_port = target_address;

  masque_session_->SendBindCapsule(capsule, encapsulated_client_session_);
  // TODO(abhisinghx): Verify Acks Before using it
  return AllocateContextId();
}

ContextId
MasqueClientSession::ConnectUdpClientState::CreateUncompressedContext() {
  bind_context_ip_map_[next_available_context_id_] =
      quiche::QuicheSocketAddress();
  quiche::Capsule capsule = quiche::Capsule::CompressionAssign();
  capsule.compression_assign_capsule().context_id = next_available_context_id_;
  capsule.compression_assign_capsule().ip_address_port =
      quiche::QuicheSocketAddress();
  QUIC_DLOG(INFO) << "Requesting open context " << next_available_context_id_;
  masque_session_->SendBindCapsule(capsule, encapsulated_client_session_);
  // TODO(abhisinghx): Verify capsule ack before using it.
  // It is fine to pre-emptively send UDP packets.
  return AllocateContextId();
}

ContextId MasqueClientSession::ConnectUdpClientState::AllocateContextId() {
  next_available_context_id_ += 2;
  return next_available_context_id_ - 2;
}

void MasqueClientSession::ConnectUdpClientState::CloseContext(
    ContextId context_id) {
  bind_context_ip_map_.erase(context_id);
  quiche::Capsule capsule = quiche::Capsule::CompressionClose();
  capsule.compression_close_capsule().context_id = context_id;
  QUIC_DLOG(INFO) << "Closing context " << context_id;
  masque_session_->SendBindCapsule(capsule, encapsulated_client_session_);
}

bool MasqueClientSession::ConnectUdpClientState::OnCompressionAssignCapsule(
    const quiche::CompressionAssignCapsule& capsule) {
  if (!is_bind_) {
    QUIC_DLOG(ERROR) << "Received CompressionAssignCapsule "
                     << capsule.ToString() << " when bind is not set";
    return false;
  }
  QUIC_DLOG(INFO) << "CompressionAssignCapsule received: "
                  << capsule.ToString();
  if (!bind_context_ip_map_.contains(capsule.context_id)) {
    // Server is asking to create a new context.
    if (capsule.ip_address_port == quiche::QuicheSocketAddress()) {
      // Server is not allowed to create open context.
      QUIC_DLOG(ERROR) << "Server is not allowed to create open context";
      return false;
    } else {
      // Compressed context requested by server.
      if (capsule.context_id % 2 == 0) {
        // Even contexts are client-allocated.
        QUIC_DLOG(ERROR) << "Server is not allowed to create even contexts";
        return false;
      }
      bind_context_ip_map_[capsule.context_id] = capsule.ip_address_port;
      // Send acknowledgement.

      quiche::Capsule to_send = quiche::Capsule::CompressionAssign();
      to_send.compression_assign_capsule() = capsule;
      masque_session_->SendBindCapsule(to_send, encapsulated_client_session_);
      return true;
    }
  } else if (bind_context_ip_map_[capsule.context_id] !=
             capsule.ip_address_port) {
    // Context already exists but the target address is different.
    QUIC_DLOG(ERROR) << "Context already exists with different target address "
                     << bind_context_ip_map_[capsule.context_id] << " and "
                     << capsule.ip_address_port;
    return false;
  } else {
    // This is an ack of client's request, ignore it.
    QUIC_DLOG(INFO) << "Ignoring ack of client's request";
    return true;
  }
}

bool MasqueClientSession::ConnectUdpClientState::OnCompressionCloseCapsule(
    const quiche::CompressionCloseCapsule& capsule) {
  if (!is_bind_) {
    QUIC_DLOG(ERROR) << "Received CompressionCloseCapsule "
                     << capsule.ToString() << " when bind is not set";
    return false;
  }
  QUIC_DLOG(INFO) << "CompressionCloseCapsule received: " << capsule.ToString();
  // If we have this context id in the map, we can close it and send ack.
  if (bind_context_ip_map_.contains(capsule.context_id)) {
    CloseContext(capsule.context_id);
  }
  return true;
}

std::optional<ContextId>
MasqueClientSession::ConnectUdpClientState::GetContextForAddress(
    const QuicSocketAddress& target_server_address) {
  std::optional<ContextId> uncompressed_context_id;
  for (const auto& [context_id, address] : bind_context_ip_map_) {
    if (address == target_server_address) {
      return context_id;
    }
    if (address == quiche::QuicheSocketAddress()) {
      uncompressed_context_id = context_id;
    }
  }
  return uncompressed_context_id;
}

std::optional<std::string>
MasqueClientSession::ConnectUdpClientState::PrepareBindPacket(
    absl::string_view packet, const QuicSocketAddress& target_address,
    bool use_uncompressed_context) {
  std::string payload_out;
  // Find existing or open context for this address.
  std::optional<ContextId> context_id_opt =
      GetContextForAddress(target_address);
  ContextId context_id;
  if (!context_id_opt.has_value()) {
    // No context exists for this address, create a new compressed context.
    context_id = use_uncompressed_context
                     ? CreateUncompressedContext()
                     : CreateCompressedContext(target_address);
  } else {
    context_id = context_id_opt.value();
  }
  size_t total_size =
      quiche::QuicheDataWriter::GetVarInt62Len(context_id) + packet.size();
  if (bind_context_ip_map_[context_id] == target_address) {
    // No need to append target info, compress it.
    payload_out.resize(total_size);
    QuicDataWriter writer(total_size, payload_out.data());
    if (!writer.WriteVarInt62(context_id)) {
      QUIC_DLOG(ERROR) << "Failed to write compressed bind packet context id "
                       << context_id;
      return std::nullopt;
    }
    if (!writer.WriteStringPiece(packet)) {
      QUIC_DLOG(ERROR)
          << "Failed to write compressed bind packet payload for context id "
          << context_id;
      return std::nullopt;
    }
  } else {
    // Append target info.
    total_size += (sizeof(uint8_t)  // Ip version.
                   + target_address.host().ToPackedString().size()  // Ip addr.
                   + sizeof(uint16_t));                             // Port.
    payload_out.resize(total_size);
    QuicDataWriter writer(total_size, payload_out.data());
    if (!writer.WriteVarInt62(context_id) ||
        !writer.WriteUInt8(
            target_address.host().AddressFamilyToInt() == AF_INET ? 4 : 6) ||
        !writer.WriteStringPiece(target_address.host().ToPackedString()) ||
        !writer.WriteUInt16(target_address.port()) ||
        !writer.WriteStringPiece(packet)) {
      QUIC_DLOG(ERROR)
          << "Failed to write uncompressed bind packet for context id "
          << context_id;
      return std::nullopt;
    }
  }
  return payload_out;
}

MasqueClientSession::ConnectIpClientState::ConnectIpClientState(
    QuicSpdyClientStream* stream,
    EncapsulatedIpSession* encapsulated_ip_session,
    MasqueClientSession* masque_session)
    : stream_(stream),
      encapsulated_ip_session_(encapsulated_ip_session),
      masque_session_(masque_session) {
  QUICHE_DCHECK_NE(masque_session_, nullptr);
  this->stream()->RegisterHttp3DatagramVisitor(this);
  this->stream()->RegisterConnectIpVisitor(this);
}

MasqueClientSession::ConnectIpClientState::~ConnectIpClientState() {
  if (stream() != nullptr) {
    stream()->UnregisterHttp3DatagramVisitor();
    stream()->UnregisterConnectIpVisitor();
  }
}

MasqueClientSession::ConnectIpClientState::ConnectIpClientState(
    MasqueClientSession::ConnectIpClientState&& other) {
  *this = std::move(other);
}

MasqueClientSession::ConnectIpClientState&
MasqueClientSession::ConnectIpClientState::operator=(
    MasqueClientSession::ConnectIpClientState&& other) {
  stream_ = other.stream_;
  encapsulated_ip_session_ = other.encapsulated_ip_session_;
  masque_session_ = other.masque_session_;
  other.stream_ = nullptr;
  if (stream() != nullptr) {
    stream()->ReplaceHttp3DatagramVisitor(this);
    stream()->ReplaceConnectIpVisitor(this);
  }
  return *this;
}

void MasqueClientSession::ConnectIpClientState::OnHttp3Datagram(
    QuicStreamId stream_id, absl::string_view payload) {
  QUICHE_DCHECK_EQ(stream_id, stream()->id());
  QuicDataReader reader(payload);
  ContextId context_id;
  if (!reader.ReadVarInt62(&context_id)) {
    QUIC_DLOG(ERROR) << "Failed to read context ID";
    return;
  }
  if (context_id != kConnectIpPayloadContextId) {
    QUIC_DLOG(ERROR) << "Ignoring HTTP Datagram with unexpected context ID "
                     << context_id;
    return;
  }
  absl::string_view http_payload = reader.ReadRemainingPayload();
  encapsulated_ip_session_->ProcessIpPacket(http_payload);
  QUIC_DVLOG(1) << "Sent " << http_payload.size()
                << " IP bytes to connection for stream ID " << stream_id;
}

bool MasqueClientSession::ConnectIpClientState::OnAddressAssignCapsule(
    const AddressAssignCapsule& capsule) {
  return encapsulated_ip_session_->OnAddressAssignCapsule(capsule);
}

bool MasqueClientSession::ConnectIpClientState::OnAddressRequestCapsule(
    const AddressRequestCapsule& capsule) {
  return encapsulated_ip_session_->OnAddressRequestCapsule(capsule);
}

bool MasqueClientSession::ConnectIpClientState::OnRouteAdvertisementCapsule(
    const RouteAdvertisementCapsule& capsule) {
  return encapsulated_ip_session_->OnRouteAdvertisementCapsule(capsule);
}

void MasqueClientSession::ConnectIpClientState::OnHeadersWritten() {}

// ConnectEthernetClientState

MasqueClientSession::ConnectEthernetClientState::ConnectEthernetClientState(
    QuicSpdyClientStream* stream,
    EncapsulatedEthernetSession* encapsulated_ethernet_session,
    MasqueClientSession* masque_session)
    : stream_(stream),
      encapsulated_ethernet_session_(encapsulated_ethernet_session),
      masque_session_(masque_session) {
  QUICHE_DCHECK_NE(masque_session_, nullptr);
  this->stream()->RegisterHttp3DatagramVisitor(this);
}

MasqueClientSession::ConnectEthernetClientState::~ConnectEthernetClientState() {
  if (stream() != nullptr) {
    stream()->UnregisterHttp3DatagramVisitor();
  }
}

MasqueClientSession::ConnectEthernetClientState::ConnectEthernetClientState(
    MasqueClientSession::ConnectEthernetClientState&& other) {
  *this = std::move(other);
}

MasqueClientSession::ConnectEthernetClientState&
MasqueClientSession::ConnectEthernetClientState::operator=(
    MasqueClientSession::ConnectEthernetClientState&& other) {
  stream_ = other.stream_;
  encapsulated_ethernet_session_ = other.encapsulated_ethernet_session_;
  masque_session_ = other.masque_session_;
  other.stream_ = nullptr;
  if (stream() != nullptr) {
    stream()->ReplaceHttp3DatagramVisitor(this);
  }
  return *this;
}

void MasqueClientSession::ConnectEthernetClientState::OnHttp3Datagram(
    QuicStreamId stream_id, absl::string_view payload) {
  QUICHE_DCHECK_EQ(stream_id, stream()->id());
  QuicDataReader reader(payload);
  ContextId context_id;
  if (!reader.ReadVarInt62(&context_id)) {
    QUIC_DLOG(ERROR) << "Failed to read context ID";
    return;
  }
  if (context_id != kConnectEthernetPayloadContextId) {
    QUIC_DLOG(ERROR) << "Ignoring HTTP Datagram with unexpected context ID "
                     << context_id;
    return;
  }
  absl::string_view http_payload = reader.ReadRemainingPayload();
  encapsulated_ethernet_session_->ProcessEthernetFrame(http_payload);
  QUIC_DVLOG(1) << "Sent " << http_payload.size()
                << " ETHERNET bytes to connection for stream ID " << stream_id;
}

// End ConnectEthernetClientState

quiche::QuicheIpAddress MasqueClientSession::GetFakeAddress(
    absl::string_view hostname) {
  QUICHE_CHECK(!hostname.empty());
  quiche::QuicheIpAddress address;
  uint8_t address_bytes[16] = {0xFD};
  quiche::QuicheRandom::GetInstance()->RandBytes(&address_bytes[1],
                                                 sizeof(address_bytes) - 1);
  address.FromPackedString(reinterpret_cast<const char*>(address_bytes),
                           sizeof(address_bytes));
  std::string address_bytes_string(reinterpret_cast<const char*>(address_bytes),
                                   sizeof(address_bytes));
  fake_addresses_[address_bytes_string] = std::string(hostname);
  return address;
}

void MasqueClientSession::RemoveFakeAddress(
    const quiche::QuicheIpAddress& fake_address) {
  fake_addresses_.erase(fake_address.ToPackedString());
}

void MasqueClientSession::EnableConcealedAuth(absl::string_view key_id,
                                              absl::string_view private_key,
                                              absl::string_view public_key) {
  QUICHE_CHECK(!key_id.empty());
  QUICHE_CHECK_EQ(private_key.size(),
                  static_cast<size_t>(ED25519_PRIVATE_KEY_LEN));
  QUICHE_CHECK_EQ(public_key.size(),
                  static_cast<size_t>(ED25519_PUBLIC_KEY_LEN));
  concealed_auth_key_id_ = key_id;
  concealed_auth_private_key_ = private_key;
  concealed_auth_public_key_ = public_key;
}

QuicSpdyClientStream* MasqueClientSession::SendGetRequest(
    absl::string_view path) {
  QuicSpdyClientStream* stream = CreateOutgoingBidirectionalStream();
  if (stream == nullptr) {
    // Stream flow control limits prevented us from opening a new stream.
    QUIC_DLOG(ERROR) << "Failed to open GET stream";
    return nullptr;
  }

  QuicUrl url(uri_template_);
  std::string scheme = url.scheme();
  std::string authority = url.HostPort();

  QUIC_DLOG(INFO) << "Sending GET request on stream " << stream->id()
                  << " scheme=\"" << scheme << "\" authority=\"" << authority
                  << "\" path=\"" << path << "\"";

  // Send the request.
  quiche::HttpHeaderBlock headers;
  headers[":method"] = "GET";
  headers[":scheme"] = scheme;
  headers[":authority"] = authority;
  headers[":path"] = path;
  AddAdditionalHeaders(headers, url);
  QUIC_DVLOG(1) << "Sending request headers: " << headers.DebugString();
  // Setting the stream visitor is required to enable reading of the response
  // body from the stream.
  stream->set_visitor(this);
  size_t bytes_sent =
      stream->SendRequest(std::move(headers), /*body=*/"", /*fin=*/true);
  if (bytes_sent == 0) {
    QUIC_DLOG(ERROR) << "Failed to send GET request";
    return nullptr;
  }
  return stream;
}

void MasqueClientSession::OnClose(QuicSpdyStream* stream) {
  QUIC_DVLOG(1) << "Closing stream " << stream->id();
}

std::optional<std::string> MasqueClientSession::ComputeConcealedAuthHeader(
    const QuicUrl& url) {
  if (concealed_auth_private_key_.empty()) {
    return std::nullopt;
  }
  std::string scheme = url.scheme();
  std::string host = url.host();
  uint16_t port = url.port();
  std::string realm = "";
  std::string key_exporter_output;
  std::string key_exporter_context = ComputeConcealedAuthContext(
      kEd25519SignatureScheme, concealed_auth_key_id_,
      concealed_auth_public_key_, scheme, host, port, realm);
  QUIC_DVLOG(1) << "key_exporter_context: "
                << absl::WebSafeBase64Escape(key_exporter_context);
  QUICHE_DCHECK(!key_exporter_context.empty());
  if (!GetMutableCryptoStream()->ExportKeyingMaterial(
          kConcealedAuthLabel, key_exporter_context, kConcealedAuthExporterSize,
          &key_exporter_output)) {
    QUIC_LOG(FATAL) << "Concealed auth TLS exporter failed";
    return std::nullopt;
  }
  QUICHE_CHECK_EQ(key_exporter_output.size(), kConcealedAuthExporterSize);
  std::string signature_input =
      key_exporter_output.substr(0, kConcealedAuthSignatureInputSize);
  QUIC_DVLOG(1) << "signature_input: "
                << absl::WebSafeBase64Escape(signature_input);
  std::string verification = key_exporter_output.substr(
      kConcealedAuthSignatureInputSize, kConcealedAuthVerificationSize);
  std::string data_covered_by_signature =
      ConcealedAuthDataCoveredBySignature(signature_input);
  QUIC_DVLOG(1) << "data_covered_by_signature: "
                << absl::WebSafeBase64Escape(data_covered_by_signature);
  uint8_t signature[ED25519_SIGNATURE_LEN];
  if (ED25519_sign(
          signature,
          reinterpret_cast<const uint8_t*>(data_covered_by_signature.data()),
          data_covered_by_signature.size(),
          reinterpret_cast<const uint8_t*>(
              concealed_auth_private_key_.data())) != 1) {
    QUIC_LOG(FATAL) << "Concealed auth signature failed";
    return std::nullopt;
  }
  return absl::StrCat(
      "Concealed k=", absl::WebSafeBase64Escape(concealed_auth_key_id_),
      ", a=", absl::WebSafeBase64Escape(concealed_auth_public_key_), ", p=",
      absl::WebSafeBase64Escape(absl::string_view(
          reinterpret_cast<const char*>(signature), sizeof(signature))),
      ", s=", kEd25519SignatureScheme,
      ", v=", absl::WebSafeBase64Escape(verification));
}

void MasqueClientSession::AddAdditionalHeaders(quiche::HttpHeaderBlock& headers,
                                               const QuicUrl& url) {
  std::optional<std::string> concealed_auth_header =
      ComputeConcealedAuthHeader(url);
  if (concealed_auth_header.has_value()) {
    headers["authorization"] = *concealed_auth_header;
  }
  if (additional_headers_.empty()) {
    return;
  }
  for (absl::string_view sp : absl::StrSplit(additional_headers_, ';')) {
    quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&sp);
    if (sp.empty()) {
      continue;
    }
    std::vector<absl::string_view> kv =
        absl::StrSplit(sp, absl::MaxSplits(':', 1));
    quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&kv[0]);
    quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&kv[1]);
    headers[kv[0]] = kv[1];
  }
}

}  // namespace quic
