// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/masque/masque_encapsulated_client_session.h"

namespace quic {

MasqueEncapsulatedClientSession::MasqueEncapsulatedClientSession(
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection,
    const QuicServerId& server_id,
    QuicCryptoClientConfig* crypto_config,
    QuicClientPushPromiseIndex* push_promise_index,
    MasqueClientSession* masque_client_session)
    : QuicSpdyClientSession(config,
                            supported_versions,
                            connection,
                            server_id,
                            crypto_config,
                            push_promise_index),
      masque_client_session_(masque_client_session) {}

void MasqueEncapsulatedClientSession::ProcessPacket(
    quiche::QuicheStringPiece packet,
    QuicSocketAddress server_address) {
  QuicTime now = connection()->clock()->ApproximateNow();
  QuicReceivedPacket received_packet(packet.data(), packet.length(), now);
  connection()->ProcessUdpPacket(connection()->self_address(), server_address,
                                 received_packet);
}

void MasqueEncapsulatedClientSession::OnConnectionClosed(
    const QuicConnectionCloseFrame& /*frame*/,
    ConnectionCloseSource /*source*/) {
  masque_client_session_->UnregisterConnectionId(
      connection()->client_connection_id());
}

}  // namespace quic
