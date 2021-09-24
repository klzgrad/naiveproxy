// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/masque/masque_client_session.h"
#include "absl/algorithm/container.h"
#include "quic/core/http/spdy_utils.h"
#include "quic/core/quic_data_reader.h"
#include "quic/core/quic_utils.h"

namespace quic {

MasqueClientSession::MasqueClientSession(
    MasqueMode masque_mode,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection,
    const QuicServerId& server_id,
    QuicCryptoClientConfig* crypto_config,
    QuicClientPushPromiseIndex* push_promise_index,
    Owner* owner)
    : QuicSpdyClientSession(config,
                            supported_versions,
                            connection,
                            server_id,
                            crypto_config,
                            push_promise_index),
      masque_mode_(masque_mode),
      owner_(owner),
      compression_engine_(this) {}

void MasqueClientSession::OnMessageReceived(absl::string_view message) {
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

    auto connection_id_registration =
        client_connection_id_registrations_.find(client_connection_id);
    if (connection_id_registration ==
        client_connection_id_registrations_.end()) {
      QUIC_DLOG(ERROR) << "MasqueClientSession failed to dispatch "
                       << client_connection_id;
      return;
    }
    EncapsulatedClientSession* encapsulated_client_session =
        connection_id_registration->second;
    encapsulated_client_session->ProcessPacket(
        absl::string_view(packet.data(), packet.size()), target_server_address);

    QUIC_DVLOG(1) << "Sent " << packet.size() << " bytes to connection for "
                  << client_connection_id;
    return;
  }
  QUICHE_DCHECK_EQ(masque_mode_, MasqueMode::kOpen);
  QuicSpdySession::OnMessageReceived(message);
}

void MasqueClientSession::OnMessageAcked(QuicMessageId message_id,
                                         QuicTime /*receive_timestamp*/) {
  QUIC_DVLOG(1) << "Received ack for DATAGRAM frame " << message_id;
}

void MasqueClientSession::OnMessageLost(QuicMessageId message_id) {
  QUIC_DVLOG(1) << "We believe DATAGRAM frame " << message_id << " was lost";
}

const MasqueClientSession::ConnectUdpClientState*
MasqueClientSession::GetOrCreateConnectUdpClientState(
    const QuicSocketAddress& target_server_address,
    EncapsulatedClientSession* encapsulated_client_session) {
  for (const ConnectUdpClientState& client_state : connect_udp_client_states_) {
    if (client_state.target_server_address() == target_server_address &&
        client_state.encapsulated_client_session() ==
            encapsulated_client_session) {
      // Found existing CONNECT-UDP request.
      return &client_state;
    }
  }
  // No CONNECT-UDP request found, create a new one.
  QuicSpdyClientStream* stream = CreateOutgoingBidirectionalStream();
  if (stream == nullptr) {
    // Stream flow control limits prevented us from opening a new stream.
    QUIC_DLOG(ERROR) << "Failed to open CONNECT-UDP stream";
    return nullptr;
  }

  QUIC_DLOG(INFO) << "Sending CONNECT-UDP request for " << target_server_address
                  << " on stream " << stream->id();

  // Send the request.
  spdy::Http2HeaderBlock headers;
  headers[":method"] = "CONNECT-UDP";
  headers[":scheme"] = "masque";
  headers[":path"] = "/";
  headers[":authority"] = target_server_address.ToString();
  SpdyUtils::AddDatagramFlowIdHeader(&headers, stream->id());
  size_t bytes_sent =
      stream->SendRequest(std::move(headers), /*body=*/"", /*fin=*/false);
  if (bytes_sent == 0) {
    QUIC_DLOG(ERROR) << "Failed to send CONNECT-UDP request";
    return nullptr;
  }

  absl::optional<QuicDatagramContextId> context_id;
  connect_udp_client_states_.push_back(
      ConnectUdpClientState(stream, encapsulated_client_session, this,
                            context_id, target_server_address));
  return &connect_udp_client_states_.back();
}

void MasqueClientSession::SendPacket(
    QuicConnectionId client_connection_id,
    QuicConnectionId server_connection_id,
    absl::string_view packet,
    const QuicSocketAddress& target_server_address,
    EncapsulatedClientSession* encapsulated_client_session) {
  if (masque_mode_ == MasqueMode::kLegacy) {
    compression_engine_.CompressAndSendPacket(packet, client_connection_id,
                                              server_connection_id,
                                              target_server_address);
    return;
  }
  const ConnectUdpClientState* connect_udp = GetOrCreateConnectUdpClientState(
      target_server_address, encapsulated_client_session);
  if (connect_udp == nullptr) {
    QUIC_DLOG(ERROR) << "Failed to create CONNECT-UDP request";
    return;
  }

  MessageStatus message_status = SendHttp3Datagram(
      connect_udp->stream()->id(), connect_udp->context_id(), packet);

  QUIC_DVLOG(1) << "Sent packet to " << target_server_address
                << " compressed with stream ID " << connect_udp->stream()->id()
                << " context ID "
                << (connect_udp->context_id().has_value()
                        ? connect_udp->context_id().value()
                        : 0)
                << " and got message status "
                << MessageStatusToString(message_status);
}

void MasqueClientSession::RegisterConnectionId(
    QuicConnectionId client_connection_id,
    EncapsulatedClientSession* encapsulated_client_session) {
  QUIC_DLOG(INFO) << "Registering " << client_connection_id
                  << " to encapsulated client";
  QUICHE_DCHECK(
      client_connection_id_registrations_.find(client_connection_id) ==
          client_connection_id_registrations_.end() ||
      client_connection_id_registrations_[client_connection_id] ==
          encapsulated_client_session);
  client_connection_id_registrations_[client_connection_id] =
      encapsulated_client_session;
}

void MasqueClientSession::UnregisterConnectionId(
    QuicConnectionId client_connection_id,
    EncapsulatedClientSession* encapsulated_client_session) {
  QUIC_DLOG(INFO) << "Unregistering " << client_connection_id;
  if (masque_mode_ == MasqueMode::kLegacy) {
    if (client_connection_id_registrations_.find(client_connection_id) !=
        client_connection_id_registrations_.end()) {
      client_connection_id_registrations_.erase(client_connection_id);
      owner_->UnregisterClientConnectionId(client_connection_id);
      compression_engine_.UnregisterClientConnectionId(client_connection_id);
    }
    return;
  }

  for (auto it = connect_udp_client_states_.begin();
       it != connect_udp_client_states_.end();) {
    if (it->encapsulated_client_session() == encapsulated_client_session) {
      QUIC_DLOG(INFO) << "Removing state for stream ID " << it->stream()->id()
                      << " context ID "
                      << (it->context_id().has_value()
                              ? it->context_id().value()
                              : 0);
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

void MasqueClientSession::OnConnectionClosed(
    const QuicConnectionCloseFrame& frame,
    ConnectionCloseSource source) {
  QuicSpdyClientSession::OnConnectionClosed(frame, source);
  // Close all encapsulated sessions.
  for (const auto& client_state : connect_udp_client_states_) {
    client_state.encapsulated_client_session()->CloseConnection(
        QUIC_CONNECTION_CANCELLED, "Underlying MASQUE connection was closed",
        ConnectionCloseBehavior::SILENT_CLOSE);
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
                      << " was closed, removing state for context ID "
                      << (it->context_id().has_value()
                              ? it->context_id().value()
                              : 0);
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

  QuicSpdyClientSession::OnStreamClosed(stream_id);
}

bool MasqueClientSession::OnSettingsFrame(const SettingsFrame& frame) {
  if (!QuicSpdyClientSession::OnSettingsFrame(frame)) {
    QUIC_DLOG(ERROR) << "Failed to parse received settings";
    return false;
  }
  if (!SupportsH3Datagram()) {
    QUIC_DLOG(ERROR) << "Refusing to use MASQUE without HTTP/3 Datagrams";
    return false;
  }
  owner_->OnSettingsReceived();
  return true;
}

MasqueClientSession::ConnectUdpClientState::ConnectUdpClientState(
    QuicSpdyClientStream* stream,
    EncapsulatedClientSession* encapsulated_client_session,
    MasqueClientSession* masque_session,
    absl::optional<QuicDatagramContextId> context_id,
    const QuicSocketAddress& target_server_address)
    : stream_(stream),
      encapsulated_client_session_(encapsulated_client_session),
      masque_session_(masque_session),
      context_id_(context_id),
      target_server_address_(target_server_address) {
  QUICHE_DCHECK_NE(masque_session_, nullptr);
  this->stream()->RegisterHttp3DatagramRegistrationVisitor(this);
  Http3DatagramContextExtensions extensions;
  this->stream()->RegisterHttp3DatagramContextId(this->context_id(), extensions,
                                                 this);
}

MasqueClientSession::ConnectUdpClientState::~ConnectUdpClientState() {
  if (stream() != nullptr) {
    stream()->UnregisterHttp3DatagramContextId(context_id());
    stream()->UnregisterHttp3DatagramRegistrationVisitor();
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
  context_id_ = other.context_id_;
  target_server_address_ = other.target_server_address_;
  other.stream_ = nullptr;
  if (stream() != nullptr) {
    stream()->MoveHttp3DatagramRegistration(this);
    stream()->MoveHttp3DatagramContextIdRegistration(context_id(), this);
  }
  return *this;
}

void MasqueClientSession::ConnectUdpClientState::OnHttp3Datagram(
    QuicStreamId stream_id, absl::optional<QuicDatagramContextId> context_id,
    absl::string_view payload) {
  QUICHE_DCHECK_EQ(stream_id, stream()->id());
  QUICHE_DCHECK(context_id == context_id_);
  encapsulated_client_session_->ProcessPacket(payload, target_server_address_);
  QUIC_DVLOG(1) << "Sent " << payload.size()
                << " bytes to connection for stream ID " << stream_id
                << " context ID "
                << (context_id.has_value() ? context_id.value() : 0);
}

void MasqueClientSession::ConnectUdpClientState::OnContextReceived(
    QuicStreamId stream_id, absl::optional<QuicDatagramContextId> context_id,
    const Http3DatagramContextExtensions& /*extensions*/) {
  if (stream_id != stream_->id()) {
    QUIC_BUG(MASQUE client bad datagram context registration)
        << "Registered stream ID " << stream_id << ", expected "
        << stream_->id();
    return;
  }
  if (context_id != context_id_) {
    QUIC_DLOG(INFO) << "Ignoring unexpected context ID "
                    << (context_id.has_value() ? context_id.value() : 0)
                    << " instead of "
                    << (context_id_.has_value() ? context_id_.value() : 0)
                    << " on stream ID " << stream_->id();
    return;
  }
  // Do nothing since the client registers first and we currently ignore
  // extensions.
}

void MasqueClientSession::ConnectUdpClientState::OnContextClosed(
    QuicStreamId stream_id, absl::optional<QuicDatagramContextId> context_id,
    const Http3DatagramContextExtensions& /*extensions*/) {
  if (stream_id != stream_->id()) {
    QUIC_BUG(MASQUE client bad datagram context registration)
        << "Closed context on stream ID " << stream_id << ", expected "
        << stream_->id();
    return;
  }
  if (context_id != context_id_) {
    QUIC_DLOG(INFO) << "Ignoring unexpected close of context ID "
                    << (context_id.has_value() ? context_id.value() : 0)
                    << " instead of "
                    << (context_id_.has_value() ? context_id_.value() : 0)
                    << " on stream ID " << stream_->id();
    return;
  }
  QUIC_DLOG(INFO) << "Received datagram context close on stream ID "
                  << stream_->id() << ", closing stream";
  masque_session_->ResetStream(stream_->id(), QUIC_STREAM_CANCELLED);
}

}  // namespace quic
