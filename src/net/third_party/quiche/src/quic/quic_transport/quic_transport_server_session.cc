// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_server_session.h"

#include <memory>

#include "url/gurl.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_protocol.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_stream.h"

namespace quic {

namespace {
class QuicTransportServerCryptoHelper : public QuicCryptoServerStream::Helper {
 public:
  bool CanAcceptClientHello(const CryptoHandshakeMessage& /*message*/,
                            const QuicSocketAddress& /*client_address*/,
                            const QuicSocketAddress& /*peer_address*/,
                            const QuicSocketAddress& /*self_address*/,
                            std::string* /*error_details*/) const override {
    return true;
  }
};
}  // namespace

QuicTransportServerSession::QuicTransportServerSession(
    QuicConnection* connection,
    Visitor* owner,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    ServerVisitor* visitor)
    : QuicSession(connection,
                  owner,
                  config,
                  supported_versions,
                  /*num_expected_unidirectional_static_streams*/ 0),
      visitor_(visitor) {
  for (const ParsedQuicVersion& version : supported_versions) {
    QUIC_BUG_IF(version.handshake_protocol != PROTOCOL_TLS1_3)
        << "QuicTransport requires TLS 1.3 handshake";
  }

  static QuicTransportServerCryptoHelper* helper =
      new QuicTransportServerCryptoHelper();
  crypto_stream_ = std::make_unique<QuicCryptoServerStream>(
      crypto_config, compressed_certs_cache, this, helper);
}

QuicStream* QuicTransportServerSession::CreateIncomingStream(QuicStreamId id) {
  if (id == ClientIndicationStream()) {
    auto indication = std::make_unique<ClientIndication>(this);
    ClientIndication* indication_ptr = indication.get();
    ActivateStream(std::move(indication));
    return indication_ptr;
  }

  auto stream = std::make_unique<QuicTransportStream>(id, this, this);
  QuicTransportStream* stream_ptr = stream.get();
  ActivateStream(std::move(stream));
  OnIncomingDataStream(stream_ptr);
  return stream_ptr;
}

QuicTransportServerSession::ClientIndication::ClientIndication(
    QuicTransportServerSession* session)
    : QuicStream(ClientIndicationStream(),
                 session,
                 /* is_static= */ false,
                 StreamType::READ_UNIDIRECTIONAL),
      session_(session) {}

void QuicTransportServerSession::ClientIndication::OnDataAvailable() {
  sequencer()->Read(&buffer_);
  if (buffer_.size() > ClientIndicationMaxSize()) {
    session_->connection()->CloseConnection(
        QUIC_TRANSPORT_INVALID_CLIENT_INDICATION,
        QuicStrCat("Client indication size exceeds ", ClientIndicationMaxSize(),
                   " bytes"),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  if (sequencer()->IsClosed()) {
    session_->ProcessClientIndication(buffer_);
    OnFinRead();
  }
}

bool QuicTransportServerSession::ClientIndicationParser::Parse() {
  bool origin_received = false;
  while (!reader_.IsDoneReading()) {
    uint16_t key;
    if (!reader_.ReadUInt16(&key)) {
      ParseError("Expected 16-bit key");
      return false;
    }

    QuicStringPiece value;
    if (!reader_.ReadStringPiece16(&value)) {
      ParseError(QuicStrCat("Failed to read value for key ", key));
      return false;
    }

    switch (static_cast<QuicTransportClientIndicationKeys>(key)) {
      case QuicTransportClientIndicationKeys::kOrigin: {
        GURL origin_url{std::string(value)};
        if (!origin_url.is_valid()) {
          Error("Unable to parse the specified origin");
          return false;
        }

        url::Origin origin = url::Origin::Create(origin_url);
        QUIC_DLOG(INFO) << "QuicTransport server received origin " << origin;
        if (!session_->visitor_->CheckOrigin(origin)) {
          Error("Origin check failed");
          return false;
        }
        origin_received = true;
        break;
      }

      default:
        QUIC_DLOG(INFO) << "Unknown client indication key: " << key;
        break;
    }
  }

  if (!origin_received) {
    Error("No origin received");
    return false;
  }

  return true;
}

void QuicTransportServerSession::ClientIndicationParser::Error(
    const std::string& error_message) {
  session_->connection()->CloseConnection(
      QUIC_TRANSPORT_INVALID_CLIENT_INDICATION, error_message,
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicTransportServerSession::ClientIndicationParser::ParseError(
    QuicStringPiece error_message) {
  Error(QuicStrCat("Failed to parse the client indication stream: ",
                   error_message, reader_.DebugString()));
}

void QuicTransportServerSession::ProcessClientIndication(
    QuicStringPiece indication) {
  ClientIndicationParser parser(this, indication);
  if (!parser.Parse()) {
    return;
  }
  // Don't set the ready bit if we closed the connection due to any error
  // beforehand.
  if (!connection()->connected()) {
    return;
  }
  ready_ = true;
}

}  // namespace quic
