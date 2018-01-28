// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/tls_client_handshaker.h"

#include "net/quic/core/crypto/quic_encrypter.h"
#include "net/quic/core/quic_session.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

using std::string;

namespace net {

TlsClientHandshaker::ProofVerifierCallbackImpl::ProofVerifierCallbackImpl(
    TlsClientHandshaker* parent)
    : parent_(parent) {}

TlsClientHandshaker::ProofVerifierCallbackImpl::~ProofVerifierCallbackImpl() {}

void TlsClientHandshaker::ProofVerifierCallbackImpl::Run(
    bool ok,
    const string& error_details,
    std::unique_ptr<ProofVerifyDetails>* details) {
  if (parent_ == nullptr) {
    return;
  }

  parent_->verify_details_ = std::move(*details);
  parent_->verify_result_ = ok ? ssl_verify_ok : ssl_verify_invalid;
  parent_->state_ = STATE_HANDSHAKE_RUNNING;
  parent_->proof_verify_callback_ = nullptr;
  parent_->AdvanceHandshake();
}

void TlsClientHandshaker::ProofVerifierCallbackImpl::Cancel() {
  parent_ = nullptr;
}

TlsClientHandshaker::TlsClientHandshaker(QuicCryptoStream* stream,
                                         QuicSession* session,
                                         const QuicServerId& server_id,
                                         ProofVerifier* proof_verifier,
                                         SSL_CTX* ssl_ctx,
                                         ProofVerifyContext* verify_context)
    : TlsHandshaker(stream, session, ssl_ctx),
      server_id_(server_id),
      proof_verifier_(proof_verifier),
      verify_context_(verify_context) {}

TlsClientHandshaker::~TlsClientHandshaker() {
  if (proof_verify_callback_) {
    proof_verify_callback_->Cancel();
  }
}

bool TlsClientHandshaker::CryptoConnect() {
  state_ = STATE_HANDSHAKE_RUNNING;
  // Configure certificate verification.
  // TODO(nharper): This only verifies certs on initial connection, not on
  // resumption. Chromium has this callback be a no-op and verifies the
  // certificate after the connection is complete. We need to re-verify on
  // resumption in case of expiration or revocation/distrust.
  SSL_set_custom_verify(ssl(), SSL_VERIFY_PEER, &VerifyCallback);

  // Configure the SSL to be a client.
  SSL_set_connect_state(ssl());
  if (SSL_set_tlsext_host_name(ssl(), server_id_.host().c_str()) != 1) {
    return false;
  }

  // Start the handshake.
  AdvanceHandshake();
  return session()->connection()->connected();
}

int TlsClientHandshaker::num_sent_client_hellos() const {
  // TODO(nharper): Return a sensible value here.
  return 0;
}

int TlsClientHandshaker::num_scup_messages_received() const {
  // SCUP messages aren't sent or received when using the TLS handshake.
  return 0;
}

bool TlsClientHandshaker::WasChannelIDSent() const {
  // Channel ID is not used with TLS in QUIC.
  return false;
}

bool TlsClientHandshaker::WasChannelIDSourceCallbackRun() const {
  // Channel ID is not used with TLS in QUIC.
  return false;
}

string TlsClientHandshaker::chlo_hash() const {
  return "";
}

bool TlsClientHandshaker::encryption_established() const {
  return encryption_established_;
}

bool TlsClientHandshaker::handshake_confirmed() const {
  return handshake_confirmed_;
}

const QuicCryptoNegotiatedParameters&
TlsClientHandshaker::crypto_negotiated_params() const {
  return *crypto_negotiated_params_;
}

CryptoMessageParser* TlsClientHandshaker::crypto_message_parser() {
  return TlsHandshaker::crypto_message_parser();
}

void TlsClientHandshaker::AdvanceHandshake() {
  if (state_ == STATE_CONNECTION_CLOSED) {
    QUIC_LOG(INFO)
        << "TlsClientHandshaker received message after connection closed";
    return;
  }
  if (state_ == STATE_IDLE) {
    CloseConnection();
    return;
  }
  if (state_ == STATE_HANDSHAKE_COMPLETE) {
    // TODO(nharper): Handle post-handshake messages.
    return;
  }

  QUIC_LOG(INFO) << "TlsClientHandshaker: continuing handshake";
  int rv = SSL_do_handshake(ssl());
  if (rv == 1) {
    FinishHandshake();
    return;
  }
  int ssl_error = SSL_get_error(ssl(), rv);
  bool should_close = true;
  switch (state_) {
    case STATE_HANDSHAKE_RUNNING:
      should_close = ssl_error != SSL_ERROR_WANT_READ;
      break;
    case STATE_CERT_VERIFY_PENDING:
      should_close = ssl_error != SSL_ERROR_WANT_CERTIFICATE_VERIFY;
      break;
    default:
      should_close = true;
  }
  if (should_close) {
    // TODO(nharper): Surface error details from the error queue when ssl_error
    // is SSL_ERROR_SSL.
    QUIC_LOG(WARNING) << "SSL_do_handshake failed; closing connection";
    CloseConnection();
  }
}

void TlsClientHandshaker::CloseConnection() {
  // TODO(nharper): Instead of QUIC_HANDSHAKE_FAILED, this should be
  // TLS_HANDSHAKE_FAILED (0xC000001C), but according to quic_error_codes.h,
  // we only send 1-byte error codes right now.
  state_ = STATE_CONNECTION_CLOSED;
  stream()->CloseConnectionWithDetails(QUIC_HANDSHAKE_FAILED,
                                       "TLS handshake failed");
}

void TlsClientHandshaker::FinishHandshake() {
  QUIC_LOG(INFO) << "Client: handshake finished";
  state_ = STATE_HANDSHAKE_COMPLETE;
  std::vector<uint8_t> client_secret, server_secret;
  if (!DeriveSecrets(ssl(), &client_secret, &server_secret)) {
    CloseConnection();
    return;
  }

  // TODO(nharper): Use |client_secret| and |server_secret| to set the
  // appropriate crypters on the connection, and set |encryption_established_|
  // to true. Whenever encryption keys are set, call
  // session()->connection()->NeuterUnencryptedPackets().
  handshake_confirmed_ = true;
}

// static
enum ssl_verify_result_t TlsClientHandshaker::VerifyCallback(
    SSL* ssl,
    uint8_t* out_alert) {
  return static_cast<TlsClientHandshaker*>(
             TlsHandshaker::HandshakerFromSsl(ssl))
      ->VerifyCert(out_alert);
}

enum ssl_verify_result_t TlsClientHandshaker::VerifyCert(uint8_t* out_alert) {
  if (verify_result_ != ssl_verify_retry ||
      state_ == STATE_CERT_VERIFY_PENDING) {
    enum ssl_verify_result_t result = verify_result_;
    verify_result_ = ssl_verify_retry;
    return result;
  }
  STACK_OF(CRYPTO_BUFFER)* cert_chain = SSL_get0_peer_certificates(ssl());
  if (cert_chain == nullptr) {
    *out_alert = SSL_AD_INTERNAL_ERROR;
    return ssl_verify_invalid;
  }
  // TODO(nharper): Pass the CRYPTO_BUFFERs into the QUIC stack to avoid copies.
  std::vector<string> certs;
  for (CRYPTO_BUFFER* cert : cert_chain) {
    certs.push_back(
        string(reinterpret_cast<const char*>(CRYPTO_BUFFER_data(cert)),
               CRYPTO_BUFFER_len(cert)));
  }

  ProofVerifierCallbackImpl* proof_verify_callback =
      new ProofVerifierCallbackImpl(this);

  QuicAsyncStatus verify_result = proof_verifier_->VerifyCertChain(
      server_id_.host(), certs, verify_context_.get(),
      &cert_verify_error_details_, &verify_details_,
      std::unique_ptr<ProofVerifierCallback>(proof_verify_callback));
  switch (verify_result) {
    case QUIC_SUCCESS:
      return ssl_verify_ok;
    case QUIC_PENDING:
      proof_verify_callback_ = proof_verify_callback;
      state_ = STATE_CERT_VERIFY_PENDING;
      return ssl_verify_retry;
    case QUIC_FAILURE:
    default:
      QUIC_LOG(INFO) << "Cert chain verification failed: "
                     << cert_verify_error_details_;
      return ssl_verify_invalid;
  }
}

}  // namespace net
