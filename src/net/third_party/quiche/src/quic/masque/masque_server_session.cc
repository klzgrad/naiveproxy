// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/masque/masque_server_session.h"

namespace quic {

MasqueServerSession::MasqueServerSession(
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection,
    QuicSession::Visitor* visitor,
    Visitor* owner,
    QuicCryptoServerStreamBase::Helper* helper,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    MasqueServerBackend* masque_server_backend)
    : QuicSimpleServerSession(config,
                              supported_versions,
                              connection,
                              visitor,
                              helper,
                              crypto_config,
                              compressed_certs_cache,
                              masque_server_backend),
      masque_server_backend_(masque_server_backend),
      owner_(owner),
      compression_engine_(this) {
  masque_server_backend_->RegisterBackendClient(connection_id(), this);
}

void MasqueServerSession::OnMessageReceived(quiche::QuicheStringPiece message) {
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

  QUIC_DVLOG(1) << "Received packet of length " << packet.size() << " for "
                << server_address << " client " << client_connection_id;

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
      server_address, nullptr);
  QUIC_DVLOG(1) << "Got " << write_result << " for " << packet.size()
                << " bytes to " << server_address;
}

void MasqueServerSession::OnMessageAcked(QuicMessageId message_id,
                                         QuicTime /*receive_timestamp*/) {
  QUIC_DVLOG(1) << "Received ack for DATAGRAM frame " << message_id;
}

void MasqueServerSession::OnMessageLost(QuicMessageId message_id) {
  QUIC_DVLOG(1) << "We believe DATAGRAM frame " << message_id << " was lost";
}

void MasqueServerSession::OnConnectionClosed(
    const QuicConnectionCloseFrame& /*frame*/,
    ConnectionCloseSource /*source*/) {
  QUIC_DLOG(INFO) << "Closing connection for " << connection_id();
  masque_server_backend_->RemoveBackendClient(connection_id());
}

std::unique_ptr<QuicBackendResponse> MasqueServerSession::HandleMasqueRequest(
    const std::string& masque_path,
    const spdy::SpdyHeaderBlock& /*request_headers*/,
    const std::string& request_body,
    QuicSimpleServerBackend::RequestHandler* /*request_handler*/) {
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
  spdy::SpdyHeaderBlock response_headers;
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
  compression_engine_.CompressAndSendPacket(
      packet_info.packet.AsStringPiece(), packet_info.destination_connection_id,
      packet_info.source_connection_id, packet_info.peer_address);
}

}  // namespace quic
