// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/masque/masque_client_session.h"

namespace quic {

MasqueClientSession::MasqueClientSession(
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
      owner_(owner),
      compression_engine_(this) {}

void MasqueClientSession::OnMessageReceived(quiche::QuicheStringPiece message) {
  QUIC_DVLOG(1) << "Received DATAGRAM frame of length " << message.length();

  QuicConnectionId client_connection_id, server_connection_id;
  QuicSocketAddress server_address;
  std::vector<char> packet;
  bool version_present;
  if (!compression_engine_.DecompressDatagram(
          message, &client_connection_id, &server_connection_id,
          &server_address, &packet, &version_present)) {
    return;
  }

  auto connection_id_registration =
      client_connection_id_registrations_.find(client_connection_id);
  if (connection_id_registration == client_connection_id_registrations_.end()) {
    QUIC_DLOG(ERROR) << "MasqueClientSession failed to dispatch "
                     << client_connection_id;
    return;
  }
  EncapsulatedClientSession* encapsulated_client_session =
      connection_id_registration->second;
  encapsulated_client_session->ProcessPacket(
      quiche::QuicheStringPiece(packet.data(), packet.size()), server_address);

  QUIC_DVLOG(1) << "Sent " << packet.size() << " bytes to connection for "
                << client_connection_id;
}

void MasqueClientSession::OnMessageAcked(QuicMessageId message_id,
                                         QuicTime /*receive_timestamp*/) {
  QUIC_DVLOG(1) << "Received ack for DATAGRAM frame " << message_id;
}

void MasqueClientSession::OnMessageLost(QuicMessageId message_id) {
  QUIC_DVLOG(1) << "We believe DATAGRAM frame " << message_id << " was lost";
}

void MasqueClientSession::SendPacket(QuicConnectionId client_connection_id,
                                     QuicConnectionId server_connection_id,
                                     quiche::QuicheStringPiece packet,
                                     const QuicSocketAddress& server_address) {
  compression_engine_.CompressAndSendPacket(
      packet, client_connection_id, server_connection_id, server_address);
}

void MasqueClientSession::RegisterConnectionId(
    QuicConnectionId client_connection_id,
    EncapsulatedClientSession* encapsulated_client_session) {
  QUIC_DLOG(INFO) << "Registering " << client_connection_id
                  << " to encapsulated client";
  DCHECK(client_connection_id_registrations_.find(client_connection_id) ==
             client_connection_id_registrations_.end() ||
         client_connection_id_registrations_[client_connection_id] ==
             encapsulated_client_session);
  client_connection_id_registrations_[client_connection_id] =
      encapsulated_client_session;
}

void MasqueClientSession::UnregisterConnectionId(
    QuicConnectionId client_connection_id) {
  QUIC_DLOG(INFO) << "Unregistering " << client_connection_id;
  if (client_connection_id_registrations_.find(client_connection_id) !=
      client_connection_id_registrations_.end()) {
    client_connection_id_registrations_.erase(client_connection_id);
    owner_->UnregisterClientConnectionId(client_connection_id);
    compression_engine_.UnregisterClientConnectionId(client_connection_id);
  }
}

}  // namespace quic
