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

  QuicDatagramFlowId flow_id = GetNextDatagramFlowId();

  QUIC_DLOG(INFO) << "Sending CONNECT-UDP request for " << target_server_address
                  << " using flow ID " << flow_id << " on stream "
                  << stream->id();

  // Send the request.
  spdy::Http2HeaderBlock headers;
  headers[":method"] = "CONNECT-UDP";
  headers[":scheme"] = "masque";
  headers[":path"] = "/";
  headers[":authority"] = target_server_address.ToString();
  SpdyUtils::AddDatagramFlowIdHeader(&headers, flow_id);
  size_t bytes_sent =
      stream->SendRequest(std::move(headers), /*body=*/"", /*fin=*/false);
  if (bytes_sent == 0) {
    QUIC_DLOG(ERROR) << "Failed to send CONNECT-UDP request";
    return nullptr;
  }

  connect_udp_client_states_.push_back(
      ConnectUdpClientState(stream, encapsulated_client_session, this, flow_id,
                            target_server_address));
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

  QuicDatagramFlowId flow_id = connect_udp->flow_id();
  MessageStatus message_status =
      SendHttp3Datagram(connect_udp->flow_id(), packet);

  QUIC_DVLOG(1) << "Sent packet to " << target_server_address
                << " compressed with flow ID " << flow_id
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
      QUIC_DLOG(INFO) << "Removing state for flow_id " << it->flow_id();
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
                      << " was closed, removing state for flow_id "
                      << it->flow_id();
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

MasqueClientSession::ConnectUdpClientState::ConnectUdpClientState(
    QuicSpdyClientStream* stream,
    EncapsulatedClientSession* encapsulated_client_session,
    MasqueClientSession* masque_session,
    QuicDatagramFlowId flow_id,
    const QuicSocketAddress& target_server_address)
    : stream_(stream),
      encapsulated_client_session_(encapsulated_client_session),
      masque_session_(masque_session),
      flow_id_(flow_id),
      target_server_address_(target_server_address) {
  QUICHE_DCHECK_NE(masque_session_, nullptr);
  masque_session_->RegisterHttp3FlowId(this->flow_id(), this);
}

MasqueClientSession::ConnectUdpClientState::~ConnectUdpClientState() {
  if (flow_id_.has_value()) {
    masque_session_->UnregisterHttp3FlowId(flow_id());
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
  flow_id_ = other.flow_id_;
  target_server_address_ = other.target_server_address_;
  other.flow_id_.reset();
  if (flow_id_.has_value()) {
    masque_session_->UnregisterHttp3FlowId(flow_id());
    masque_session_->RegisterHttp3FlowId(flow_id(), this);
  }
  return *this;
}

void MasqueClientSession::ConnectUdpClientState::OnHttp3Datagram(
    QuicDatagramFlowId flow_id,
    absl::string_view payload) {
  QUICHE_DCHECK_EQ(flow_id, this->flow_id());
  encapsulated_client_session_->ProcessPacket(payload, target_server_address_);
  QUIC_DVLOG(1) << "Sent " << payload.size()
                << " bytes to connection for flow_id " << flow_id;
}

}  // namespace quic
