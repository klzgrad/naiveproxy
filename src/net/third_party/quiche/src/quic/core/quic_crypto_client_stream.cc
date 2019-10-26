// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_crypto_client_stream.h"

#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_utils.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_client_handshaker.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/tls_client_handshaker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"

namespace quic {

const int QuicCryptoClientStream::kMaxClientHellos;

QuicCryptoClientStreamBase::QuicCryptoClientStreamBase(QuicSession* session)
    : QuicCryptoStream(session) {}

QuicCryptoClientStream::QuicCryptoClientStream(
    const QuicServerId& server_id,
    QuicSession* session,
    std::unique_ptr<ProofVerifyContext> verify_context,
    QuicCryptoClientConfig* crypto_config,
    ProofHandler* proof_handler)
    : QuicCryptoClientStreamBase(session) {
  DCHECK_EQ(Perspective::IS_CLIENT, session->connection()->perspective());
  switch (session->connection()->version().handshake_protocol) {
    case PROTOCOL_QUIC_CRYPTO:
      handshaker_ = QuicMakeUnique<QuicCryptoClientHandshaker>(
          server_id, this, session, std::move(verify_context), crypto_config,
          proof_handler);
      break;
    case PROTOCOL_TLS1_3:
      handshaker_ = QuicMakeUnique<TlsClientHandshaker>(
          this, session, server_id, crypto_config->proof_verifier(),
          crypto_config->ssl_ctx(), std::move(verify_context),
          crypto_config->user_agent_id());
      break;
    case PROTOCOL_UNSUPPORTED:
      QUIC_BUG << "Attempting to create QuicCryptoClientStream for unknown "
                  "handshake protocol";
  }
}

QuicCryptoClientStream::~QuicCryptoClientStream() {}

bool QuicCryptoClientStream::CryptoConnect() {
  return handshaker_->CryptoConnect();
}

int QuicCryptoClientStream::num_sent_client_hellos() const {
  return handshaker_->num_sent_client_hellos();
}

int QuicCryptoClientStream::num_scup_messages_received() const {
  return handshaker_->num_scup_messages_received();
}

bool QuicCryptoClientStream::encryption_established() const {
  return handshaker_->encryption_established();
}

bool QuicCryptoClientStream::handshake_confirmed() const {
  return handshaker_->handshake_confirmed();
}

const QuicCryptoNegotiatedParameters&
QuicCryptoClientStream::crypto_negotiated_params() const {
  return handshaker_->crypto_negotiated_params();
}

CryptoMessageParser* QuicCryptoClientStream::crypto_message_parser() {
  return handshaker_->crypto_message_parser();
}

size_t QuicCryptoClientStream::BufferSizeLimitForLevel(
    EncryptionLevel level) const {
  return handshaker_->BufferSizeLimitForLevel(level);
}

std::string QuicCryptoClientStream::chlo_hash() const {
  return handshaker_->chlo_hash();
}

}  // namespace quic
