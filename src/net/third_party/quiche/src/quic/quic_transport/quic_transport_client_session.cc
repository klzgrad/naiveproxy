// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_client_session.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "url/gurl.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_client_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {

namespace {
// ProofHandler is primarily used by QUIC crypto to persist QUIC server configs
// and perform some of related debug logging.  QuicTransport does not support
// QUIC crypto, so those methods are not called.
class DummyProofHandler : public QuicCryptoClientStream::ProofHandler {
 public:
  void OnProofValid(
      const QuicCryptoClientConfig::CachedState& /*cached*/) override {}
  void OnProofVerifyDetailsAvailable(
      const ProofVerifyDetails& /*verify_details*/) override {}
};
}  // namespace

QuicTransportClientSession::QuicTransportClientSession(
    QuicConnection* connection,
    Visitor* owner,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    const QuicServerId& server_id,
    QuicCryptoClientConfig* crypto_config,
    url::Origin origin)
    : QuicSession(connection,
                  owner,
                  config,
                  supported_versions,
                  /*num_expected_unidirectional_static_streams*/ 0),
      origin_(origin) {
  for (const ParsedQuicVersion& version : supported_versions) {
    QUIC_BUG_IF(version.handshake_protocol != PROTOCOL_TLS1_3)
        << "QuicTransport requires TLS 1.3 handshake";
  }
  // ProofHandler API is not used by TLS 1.3.
  static DummyProofHandler* proof_handler = new DummyProofHandler();
  crypto_stream_ = std::make_unique<QuicCryptoClientStream>(
      server_id, this, crypto_config->proof_verifier()->CreateDefaultContext(),
      crypto_config, proof_handler);
}

void QuicTransportClientSession::OnCryptoHandshakeEvent(
    CryptoHandshakeEvent event) {
  QuicSession::OnCryptoHandshakeEvent(event);
  if (event != HANDSHAKE_CONFIRMED) {
    return;
  }

  SendClientIndication();
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

  std::string buffer;
  buffer.resize(/* key */ sizeof(QuicTransportClientIndicationKeys) +
                /* length */ sizeof(uint16_t) + serialized_origin.size());
  QuicDataWriter writer(buffer.size(), &buffer[0]);
  writer.WriteUInt16(
      static_cast<uint16_t>(QuicTransportClientIndicationKeys::kOrigin));
  writer.WriteUInt16(serialized_origin.size());
  writer.WriteStringPiece(serialized_origin);

  buffer.resize(writer.length());
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
      /*stream_id=*/ClientIndicationStream(), this, /*is_static=*/false,
      WRITE_UNIDIRECTIONAL);
  ClientIndication* client_indication = client_indication_owned.get();
  ActivateStream(std::move(client_indication_owned));

  client_indication->WriteOrBufferData(SerializeClientIndication(),
                                       /*fin=*/true, nullptr);
  client_indication_sent_ = true;

  // Don't set the ready bit if we closed the connection due to any error
  // beforehand.
  if (!connection()->connected()) {
    return;
  }
  ready_ = true;
}

}  // namespace quic
