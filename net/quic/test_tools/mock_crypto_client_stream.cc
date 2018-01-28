// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/mock_crypto_client_stream.h"

#include "net/quic/core/crypto/null_decrypter.h"
#include "net/quic/core/crypto/null_encrypter.h"
#include "net/quic/core/crypto/quic_decrypter.h"
#include "net/quic/core/crypto/quic_encrypter.h"
#include "net/quic/core/quic_spdy_client_session_base.h"
#include "net/quic/test_tools/mock_decrypter.h"
#include "net/quic/test_tools/mock_encrypter.h"
#include "net/quic/test_tools/quic_config_peer.h"
#include "testing/gtest/include/gtest/gtest.h"

using std::string;

namespace net {

MockCryptoClientStream::MockCryptoClientStream(
    const QuicServerId& server_id,
    QuicSpdyClientSessionBase* session,
    ProofVerifyContext* verify_context,
    const QuicConfig& config,
    QuicCryptoClientConfig* crypto_config,
    HandshakeMode handshake_mode,
    const ProofVerifyDetailsChromium* proof_verify_details,
    bool use_mock_crypter)
    : QuicCryptoClientStream(server_id,
                             session,
                             verify_context,
                             crypto_config,
                             session),
      QuicCryptoHandshaker(this, session),
      handshake_mode_(handshake_mode),
      encryption_established_(false),
      handshake_confirmed_(false),
      crypto_negotiated_params_(new QuicCryptoNegotiatedParameters),
      use_mock_crypter_(use_mock_crypter),
      server_id_(server_id),
      proof_verify_details_(proof_verify_details),
      config_(config) {
  crypto_framer_.set_visitor(this);
}

MockCryptoClientStream::~MockCryptoClientStream() {}

void MockCryptoClientStream::OnHandshakeMessage(
    const CryptoHandshakeMessage& message) {
  CloseConnectionWithDetails(QUIC_CRYPTO_MESSAGE_AFTER_HANDSHAKE_COMPLETE,
                             "Forced mock failure");
}

bool MockCryptoClientStream::CryptoConnect() {
  if (proof_verify_details_) {
    if (!proof_verify_details_->cert_verify_result.verified_cert
             ->VerifyNameMatch(server_id_.host(), false)) {
      handshake_confirmed_ = false;
      encryption_established_ = false;
      session()->connection()->CloseConnection(
          QUIC_PROOF_INVALID, "proof invalid",
          ConnectionCloseBehavior::SILENT_CLOSE);
      return false;
    }
  }

  switch (handshake_mode_) {
    case ZERO_RTT: {
      encryption_established_ = true;
      handshake_confirmed_ = false;
      crypto_negotiated_params_->key_exchange = kC255;
      crypto_negotiated_params_->aead = kAESG;
      if (proof_verify_details_) {
        reinterpret_cast<QuicSpdyClientSessionBase*>(session())
            ->OnProofVerifyDetailsAvailable(*proof_verify_details_);
      }
      if (use_mock_crypter_) {
        session()->connection()->SetDecrypter(
            ENCRYPTION_INITIAL, new MockDecrypter(Perspective::IS_CLIENT));
        session()->connection()->SetEncrypter(
            ENCRYPTION_INITIAL, new MockEncrypter(Perspective::IS_CLIENT));
      } else {
        session()->connection()->SetDecrypter(
            ENCRYPTION_INITIAL, new NullDecrypter(Perspective::IS_CLIENT));
        session()->connection()->SetEncrypter(
            ENCRYPTION_INITIAL, new NullEncrypter(Perspective::IS_CLIENT));
      }
      session()->connection()->SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
      session()->OnCryptoHandshakeEvent(
          QuicSession::ENCRYPTION_FIRST_ESTABLISHED);
      break;
    }

    case CONFIRM_HANDSHAKE: {
      encryption_established_ = true;
      handshake_confirmed_ = true;
      crypto_negotiated_params_->key_exchange = kC255;
      crypto_negotiated_params_->aead = kAESG;
      if (proof_verify_details_) {
        reinterpret_cast<QuicSpdyClientSessionBase*>(session())
            ->OnProofVerifyDetailsAvailable(*proof_verify_details_);
      }
      SetConfigNegotiated();
      if (use_mock_crypter_) {
        session()->connection()->SetDecrypter(
            ENCRYPTION_FORWARD_SECURE,
            new MockDecrypter(Perspective::IS_CLIENT));
        session()->connection()->SetEncrypter(
            ENCRYPTION_FORWARD_SECURE,
            new MockEncrypter(Perspective::IS_CLIENT));
      } else {
        session()->connection()->SetDecrypter(
            ENCRYPTION_FORWARD_SECURE,
            new NullDecrypter(Perspective::IS_CLIENT));
        session()->connection()->SetEncrypter(
            ENCRYPTION_FORWARD_SECURE,
            new NullEncrypter(Perspective::IS_CLIENT));
      }
      session()->connection()->SetDefaultEncryptionLevel(
          ENCRYPTION_FORWARD_SECURE);
      session()->OnCryptoHandshakeEvent(QuicSession::HANDSHAKE_CONFIRMED);
      break;
    }

    case COLD_START: {
      handshake_confirmed_ = false;
      encryption_established_ = false;
      break;
    }

    case USE_DEFAULT_CRYPTO_STREAM: {
      NOTREACHED();
      break;
    }
  }

  return session()->connection()->connected();
}

bool MockCryptoClientStream::encryption_established() const {
  return encryption_established_;
}

bool MockCryptoClientStream::handshake_confirmed() const {
  return handshake_confirmed_;
}

const QuicCryptoNegotiatedParameters&
MockCryptoClientStream::crypto_negotiated_params() const {
  return *crypto_negotiated_params_;
}

CryptoMessageParser* MockCryptoClientStream::crypto_message_parser() {
  return &crypto_framer_;
}

void MockCryptoClientStream::SendOnCryptoHandshakeEvent(
    QuicSession::CryptoHandshakeEvent event) {
  encryption_established_ = true;
  if (event == QuicSession::HANDSHAKE_CONFIRMED) {
    handshake_confirmed_ = true;
    SetConfigNegotiated();
    if (use_mock_crypter_) {
      session()->connection()->SetDecrypter(
          ENCRYPTION_FORWARD_SECURE, new MockDecrypter(Perspective::IS_CLIENT));
      session()->connection()->SetEncrypter(
          ENCRYPTION_FORWARD_SECURE, new MockEncrypter(Perspective::IS_CLIENT));
    } else {
      session()->connection()->SetDecrypter(
          ENCRYPTION_FORWARD_SECURE, new NullDecrypter(Perspective::IS_CLIENT));
      session()->connection()->SetEncrypter(
          ENCRYPTION_FORWARD_SECURE, new NullEncrypter(Perspective::IS_CLIENT));
    }
    session()->connection()->SetDefaultEncryptionLevel(
        ENCRYPTION_FORWARD_SECURE);
  }
  session()->OnCryptoHandshakeEvent(event);
}

void MockCryptoClientStream::SetConfigNegotiated() {
  ASSERT_FALSE(session()->config()->negotiated());
  QuicTagVector cgst;
// TODO(rtenneti): Enable the following code after BBR code is checked in.
#if 0
  cgst.push_back(kTBBR);
#endif
  cgst.push_back(kQBIC);
  QuicConfig config(config_);
  config.SetIdleNetworkTimeout(
      QuicTime::Delta::FromSeconds(2 * kMaximumIdleTimeoutSecs),
      QuicTime::Delta::FromSeconds(kMaximumIdleTimeoutSecs));
  config.SetMaxStreamsPerConnection(kDefaultMaxStreamsPerConnection / 2,
                                    kDefaultMaxStreamsPerConnection / 2);
  config.SetBytesForConnectionIdToSend(PACKET_8BYTE_CONNECTION_ID);
  config.SetMaxIncomingDynamicStreamsToSend(kDefaultMaxStreamsPerConnection /
                                            2);

  CryptoHandshakeMessage msg;
  config.ToHandshakeMessage(&msg);
  string error_details;
  const QuicErrorCode error =
      session()->config()->ProcessPeerHello(msg, CLIENT, &error_details);
  ASSERT_EQ(QUIC_NO_ERROR, error);
  ASSERT_TRUE(session()->config()->negotiated());
  session()->OnConfigNegotiated();
}

}  // namespace net
