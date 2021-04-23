// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/quic_transport/quic_transport_client_session.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "url/gurl.h"
#include "quic/core/quic_constants.h"
#include "quic/core/quic_crypto_client_stream.h"
#include "quic/core/quic_data_writer.h"
#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_session.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_bug_tracker.h"
#include "quic/platform/api/quic_logging.h"
#include "quic/quic_transport/quic_transport_protocol.h"
#include "quic/quic_transport/quic_transport_stream.h"

namespace quic {

QuicTransportClientSession::QuicTransportClientSession(
    QuicConnection* connection,
    Visitor* owner,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    const GURL& url,
    QuicCryptoClientConfig* crypto_config,
    url::Origin origin,
    ClientVisitor* visitor,
    std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer)
    : QuicSession(connection,
                  owner,
                  config,
                  supported_versions,
                  /*num_expected_unidirectional_static_streams*/ 0,
                  std::move(datagram_observer)),
      url_(url),
      origin_(origin),
      visitor_(visitor) {
  for (const ParsedQuicVersion& version : supported_versions) {
    QUIC_BUG_IF(version.handshake_protocol != PROTOCOL_TLS1_3)
        << "QuicTransport requires TLS 1.3 handshake";
  }
  crypto_stream_ = std::make_unique<QuicCryptoClientStream>(
      QuicServerId(url.host(), url.EffectiveIntPort()), this,
      crypto_config->proof_verifier()->CreateDefaultContext(), crypto_config,
      /*proof_handler=*/this, /*has_application_state = */ true);
}

void QuicTransportClientSession::OnAlpnSelected(absl::string_view alpn) {
  // Defense in-depth: ensure the ALPN selected is the desired one.
  if (alpn != QuicTransportAlpn()) {
    QUIC_BUG << "QuicTransport negotiated non-QuicTransport ALPN: " << alpn;
    connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, "QuicTransport negotiated non-QuicTransport ALPN",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  alpn_received_ = true;
}

QuicStream* QuicTransportClientSession::CreateIncomingStream(QuicStreamId id) {
  QUIC_DVLOG(1) << "Creating incoming QuicTransport stream " << id;
  QuicTransportStream* stream = CreateStream(id);
  if (stream->type() == BIDIRECTIONAL) {
    incoming_bidirectional_streams_.push_back(stream);
    visitor_->OnIncomingBidirectionalStreamAvailable();
  } else {
    incoming_unidirectional_streams_.push_back(stream);
    visitor_->OnIncomingUnidirectionalStreamAvailable();
  }
  return stream;
}

void QuicTransportClientSession::SetDefaultEncryptionLevel(
    EncryptionLevel level) {
  QuicSession::SetDefaultEncryptionLevel(level);
  if (level == ENCRYPTION_FORWARD_SECURE) {
    SendClientIndication();
  }
}

void QuicTransportClientSession::OnTlsHandshakeComplete() {
  QuicSession::OnTlsHandshakeComplete();
  SendClientIndication();
}

QuicTransportStream*
QuicTransportClientSession::AcceptIncomingBidirectionalStream() {
  if (incoming_bidirectional_streams_.empty()) {
    return nullptr;
  }
  QuicTransportStream* stream = incoming_bidirectional_streams_.front();
  incoming_bidirectional_streams_.pop_front();
  return stream;
}

QuicTransportStream*
QuicTransportClientSession::AcceptIncomingUnidirectionalStream() {
  if (incoming_unidirectional_streams_.empty()) {
    return nullptr;
  }
  QuicTransportStream* stream = incoming_unidirectional_streams_.front();
  incoming_unidirectional_streams_.pop_front();
  return stream;
}

QuicTransportStream*
QuicTransportClientSession::OpenOutgoingBidirectionalStream() {
  if (!CanOpenNextOutgoingBidirectionalStream()) {
    QUIC_BUG << "Attempted to open a stream in violation of flow control";
    return nullptr;
  }
  return CreateStream(GetNextOutgoingBidirectionalStreamId());
}

QuicTransportStream*
QuicTransportClientSession::OpenOutgoingUnidirectionalStream() {
  if (!CanOpenNextOutgoingUnidirectionalStream()) {
    QUIC_BUG << "Attempted to open a stream in violation of flow control";
    return nullptr;
  }
  return CreateStream(GetNextOutgoingUnidirectionalStreamId());
}

QuicTransportStream* QuicTransportClientSession::CreateStream(QuicStreamId id) {
  auto stream = std::make_unique<QuicTransportStream>(id, this, this);
  QuicTransportStream* stream_ptr = stream.get();
  ActivateStream(std::move(stream));
  return stream_ptr;
}

std::string QuicTransportClientSession::SerializeClientIndication() {
  std::string serialized_origin = origin_.Serialize();
  if (serialized_origin.size() > std::numeric_limits<uint16_t>::max()) {
    QUIC_BUG << "Client origin too long";
    connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, "Client origin too long",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return "";
  }
  QUIC_DLOG(INFO) << "Sending client indication with origin "
                  << serialized_origin;

  std::string path = url_.PathForRequest();
  if (path.size() > std::numeric_limits<uint16_t>::max()) {
    connection()->CloseConnection(
        QUIC_TRANSPORT_INVALID_CLIENT_INDICATION, "Requested URL path too long",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return "";
  }

  constexpr size_t kPrefixSize =
      sizeof(QuicTransportClientIndicationKeys) + sizeof(uint16_t);
  const size_t buffer_size =
      2 * kPrefixSize + serialized_origin.size() + path.size();
  if (buffer_size > std::numeric_limits<uint16_t>::max()) {
    connection()->CloseConnection(
        QUIC_TRANSPORT_INVALID_CLIENT_INDICATION,
        "Client indication size limit exceeded",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return "";
  }

  std::string buffer;
  buffer.resize(buffer_size);
  QuicDataWriter writer(buffer.size(), &buffer[0]);
  bool success =
      writer.WriteUInt16(
          static_cast<uint16_t>(QuicTransportClientIndicationKeys::kOrigin)) &&
      writer.WriteUInt16(serialized_origin.size()) &&
      writer.WriteStringPiece(serialized_origin) &&
      writer.WriteUInt16(
          static_cast<uint16_t>(QuicTransportClientIndicationKeys::kPath)) &&
      writer.WriteUInt16(path.size()) && writer.WriteStringPiece(path);
  QUIC_BUG_IF(!success) << "Failed to serialize client indication";
  QUIC_BUG_IF(writer.length() != buffer.length())
      << "Serialized client indication has length different from expected";
  return buffer;
}

void QuicTransportClientSession::SendClientIndication() {
  if (!crypto_stream_->encryption_established()) {
    QUIC_BUG << "Client indication may only be sent once the encryption is "
                "established.";
    connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, "Attempted to send client indication unencrypted",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  if (ready_) {
    QUIC_BUG << "Client indication may only be sent once.";
    connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, "Attempted to send client indication twice",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  auto client_indication_owned = std::make_unique<ClientIndication>(
      /*stream_id=*/GetNextOutgoingUnidirectionalStreamId(), this,
      /*is_static=*/false, WRITE_UNIDIRECTIONAL);
  QUIC_BUG_IF(client_indication_owned->id() != ClientIndicationStream())
      << "Client indication stream is " << client_indication_owned->id()
      << " instead of expected " << ClientIndicationStream();
  ClientIndication* client_indication = client_indication_owned.get();
  ActivateStream(std::move(client_indication_owned));

  client_indication->WriteOrBufferData(SerializeClientIndication(),
                                       /*fin=*/true, nullptr);
  client_indication_sent_ = true;

  // Defense in depth: never set the ready bit unless ALPN has been confirmed.
  if (!alpn_received_) {
    QUIC_BUG << "ALPN confirmation missing after handshake complete";
    connection()->CloseConnection(
        QUIC_INTERNAL_ERROR,
        "ALPN confirmation missing after handshake complete",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  // Don't set the ready bit if we closed the connection due to any error
  // beforehand.
  if (!connection()->connected()) {
    return;
  }

  ready_ = true;
  visitor_->OnSessionReady();
}

void QuicTransportClientSession::OnMessageReceived(absl::string_view message) {
  visitor_->OnDatagramReceived(message);
}

void QuicTransportClientSession::OnCanCreateNewOutgoingStream(
    bool unidirectional) {
  if (unidirectional) {
    visitor_->OnCanCreateNewOutgoingUnidirectionalStream();
  } else {
    visitor_->OnCanCreateNewOutgoingBidirectionalStream();
  }
}

void QuicTransportClientSession::OnProofValid(
    const QuicCryptoClientConfig::CachedState& /*cached*/) {}

void QuicTransportClientSession::OnProofVerifyDetailsAvailable(
    const ProofVerifyDetails& /*verify_details*/) {}

}  // namespace quic
