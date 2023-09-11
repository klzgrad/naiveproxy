// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/tls_handshaker.h"

#include "absl/base/macros.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/crypto.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/quic_crypto_stream.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_stack_trace.h"

namespace quic {

#define ENDPOINT (SSL_is_server(ssl()) ? "TlsServer: " : "TlsClient: ")

TlsHandshaker::ProofVerifierCallbackImpl::ProofVerifierCallbackImpl(
    TlsHandshaker* parent)
    : parent_(parent) {}

TlsHandshaker::ProofVerifierCallbackImpl::~ProofVerifierCallbackImpl() {}

void TlsHandshaker::ProofVerifierCallbackImpl::Run(
    bool ok, const std::string& /*error_details*/,
    std::unique_ptr<ProofVerifyDetails>* details) {
  if (parent_ == nullptr) {
    return;
  }

  parent_->verify_details_ = std::move(*details);
  parent_->verify_result_ = ok ? ssl_verify_ok : ssl_verify_invalid;
  parent_->set_expected_ssl_error(SSL_ERROR_WANT_READ);
  parent_->proof_verify_callback_ = nullptr;
  if (parent_->verify_details_) {
    parent_->OnProofVerifyDetailsAvailable(*parent_->verify_details_);
  }
  parent_->AdvanceHandshake();
}

void TlsHandshaker::ProofVerifierCallbackImpl::Cancel() { parent_ = nullptr; }

TlsHandshaker::TlsHandshaker(QuicCryptoStream* stream, QuicSession* session)
    : stream_(stream), handshaker_delegate_(session) {}

TlsHandshaker::~TlsHandshaker() {
  if (proof_verify_callback_) {
    proof_verify_callback_->Cancel();
  }
}

bool TlsHandshaker::ProcessInput(absl::string_view input,
                                 EncryptionLevel level) {
  if (parser_error_ != QUIC_NO_ERROR) {
    return false;
  }
  // TODO(nharper): Call SSL_quic_read_level(ssl()) and check whether the
  // encryption level BoringSSL expects matches the encryption level that we
  // just received input at. If they mismatch, should ProcessInput return true
  // or false? If data is for a future encryption level, it should be queued for
  // later?
  if (SSL_provide_quic_data(ssl(), TlsConnection::BoringEncryptionLevel(level),
                            reinterpret_cast<const uint8_t*>(input.data()),
                            input.size()) != 1) {
    // SSL_provide_quic_data can fail for 3 reasons:
    // - API misuse (calling it before SSL_set_custom_quic_method, which we
    //   call in the TlsHandshaker c'tor)
    // - Memory exhaustion when appending data to its buffer
    // - Data provided at the wrong encryption level
    //
    // Of these, the only sensible error to handle is data provided at the wrong
    // encryption level.
    //
    // Note: the error provided below has a good-sounding enum value, although
    // it doesn't match the description as it's a QUIC Crypto specific error.
    parser_error_ = QUIC_INVALID_CRYPTO_MESSAGE_TYPE;
    parser_error_detail_ = "TLS stack failed to receive data";
    return false;
  }
  AdvanceHandshake();
  return true;
}

void TlsHandshaker::AdvanceHandshake() {
  if (is_connection_closed()) {
    return;
  }
  if (GetHandshakeState() >= HANDSHAKE_COMPLETE) {
    ProcessPostHandshakeMessage();
    return;
  }

  QUICHE_BUG_IF(
      quic_tls_server_async_done_no_flusher,
      SSL_is_server(ssl()) && !handshaker_delegate_->PacketFlusherAttached())
      << "is_server:" << SSL_is_server(ssl());

  QUIC_VLOG(1) << ENDPOINT << "Continuing handshake";
  last_tls_alert_.reset();
  int rv = SSL_do_handshake(ssl());

  if (is_connection_closed()) {
    return;
  }

  // If SSL_do_handshake return success(1) and we are in early data, it is
  // possible that we have provided ServerHello to BoringSSL but it hasn't been
  // processed. Retry SSL_do_handshake once will advance the handshake more in
  // that case. If there are no unprocessed ServerHello, the retry will return a
  // non-positive number.
  if (rv == 1 && SSL_in_early_data(ssl())) {
    OnEnterEarlyData();
    rv = SSL_do_handshake(ssl());

    if (is_connection_closed()) {
      return;
    }

    QUIC_VLOG(1) << ENDPOINT
                 << "SSL_do_handshake returned when entering early data. After "
                 << "retry, rv=" << rv
                 << ", SSL_in_early_data=" << SSL_in_early_data(ssl());
    // The retry should either
    // - Return <= 0 if the handshake is still pending, likely still in early
    //   data.
    // - Return 1 if the handshake has _actually_ finished. i.e.
    //   SSL_in_early_data should be false.
    //
    // In either case, it should not both return 1 and stay in early data.
    if (rv == 1 && SSL_in_early_data(ssl()) && !is_connection_closed()) {
      QUIC_BUG(quic_handshaker_stay_in_early_data)
          << "The original and the retry of SSL_do_handshake both returned "
             "success and in early data";
      CloseConnection(QUIC_HANDSHAKE_FAILED,
                      "TLS handshake failed: Still in early data after retry");
      return;
    }
  }

  if (rv == 1) {
    FinishHandshake();
    return;
  }
  int ssl_error = SSL_get_error(ssl(), rv);
  if (ssl_error == expected_ssl_error_) {
    return;
  }
  if (ShouldCloseConnectionOnUnexpectedError(ssl_error) &&
      !is_connection_closed()) {
    QUIC_VLOG(1) << "SSL_do_handshake failed; SSL_get_error returns "
                 << ssl_error;
    ERR_print_errors_fp(stderr);
    if (last_tls_alert_.has_value()) {
      std::string error_details =
          absl::StrCat("TLS handshake failure (",
                       EncryptionLevelToString(last_tls_alert_->level), ") ",
                       static_cast<int>(last_tls_alert_->desc), ": ",
                       SSL_alert_desc_string_long(last_tls_alert_->desc));
      QUIC_DLOG(ERROR) << error_details;
      CloseConnection(TlsAlertToQuicErrorCode(last_tls_alert_->desc),
                      static_cast<QuicIetfTransportErrorCodes>(
                          CRYPTO_ERROR_FIRST + last_tls_alert_->desc),
                      error_details);
    } else {
      CloseConnection(QUIC_HANDSHAKE_FAILED, "TLS handshake failed");
    }
  }
}

void TlsHandshaker::CloseConnection(QuicErrorCode error,
                                    const std::string& reason_phrase) {
  QUICHE_DCHECK(!reason_phrase.empty());
  stream()->OnUnrecoverableError(error, reason_phrase);
  is_connection_closed_ = true;
}

void TlsHandshaker::CloseConnection(QuicErrorCode error,
                                    QuicIetfTransportErrorCodes ietf_error,
                                    const std::string& reason_phrase) {
  QUICHE_DCHECK(!reason_phrase.empty());
  stream()->OnUnrecoverableError(error, ietf_error, reason_phrase);
  is_connection_closed_ = true;
}

void TlsHandshaker::OnConnectionClosed(QuicErrorCode /*error*/,
                                       ConnectionCloseSource /*source*/) {
  is_connection_closed_ = true;
}

bool TlsHandshaker::ShouldCloseConnectionOnUnexpectedError(int /*ssl_error*/) {
  return true;
}

size_t TlsHandshaker::BufferSizeLimitForLevel(EncryptionLevel level) const {
  return SSL_quic_max_handshake_flight_len(
      ssl(), TlsConnection::BoringEncryptionLevel(level));
}

ssl_early_data_reason_t TlsHandshaker::EarlyDataReason() const {
  return SSL_get_early_data_reason(ssl());
}

const EVP_MD* TlsHandshaker::Prf(const SSL_CIPHER* cipher) {
#if BORINGSSL_API_VERSION >= 23
  return SSL_CIPHER_get_handshake_digest(cipher);
#else
  return EVP_get_digestbynid(SSL_CIPHER_get_prf_nid(cipher));
#endif
}

enum ssl_verify_result_t TlsHandshaker::VerifyCert(uint8_t* out_alert) {
  if (verify_result_ != ssl_verify_retry ||
      expected_ssl_error() == SSL_ERROR_WANT_CERTIFICATE_VERIFY) {
    enum ssl_verify_result_t result = verify_result_;
    verify_result_ = ssl_verify_retry;
    *out_alert = cert_verify_tls_alert_;
    return result;
  }
  const STACK_OF(CRYPTO_BUFFER)* cert_chain = SSL_get0_peer_certificates(ssl());
  if (cert_chain == nullptr) {
    *out_alert = SSL_AD_INTERNAL_ERROR;
    return ssl_verify_invalid;
  }
  // TODO(nharper): Pass the CRYPTO_BUFFERs into the QUIC stack to avoid copies.
  std::vector<std::string> certs;
  for (CRYPTO_BUFFER* cert : cert_chain) {
    certs.push_back(
        std::string(reinterpret_cast<const char*>(CRYPTO_BUFFER_data(cert)),
                    CRYPTO_BUFFER_len(cert)));
  }
  QUIC_DVLOG(1) << "VerifyCert: peer cert_chain length: " << certs.size();

  ProofVerifierCallbackImpl* proof_verify_callback =
      new ProofVerifierCallbackImpl(this);

  cert_verify_tls_alert_ = *out_alert;
  QuicAsyncStatus verify_result = VerifyCertChain(
      certs, &cert_verify_error_details_, &verify_details_,
      &cert_verify_tls_alert_,
      std::unique_ptr<ProofVerifierCallback>(proof_verify_callback));
  switch (verify_result) {
    case QUIC_SUCCESS:
      if (verify_details_) {
        OnProofVerifyDetailsAvailable(*verify_details_);
      }
      return ssl_verify_ok;
    case QUIC_PENDING:
      proof_verify_callback_ = proof_verify_callback;
      set_expected_ssl_error(SSL_ERROR_WANT_CERTIFICATE_VERIFY);
      return ssl_verify_retry;
    case QUIC_FAILURE:
    default:
      *out_alert = cert_verify_tls_alert_;
      QUIC_LOG(INFO) << "Cert chain verification failed: "
                     << cert_verify_error_details_;
      return ssl_verify_invalid;
  }
}

void TlsHandshaker::SetWriteSecret(EncryptionLevel level,
                                   const SSL_CIPHER* cipher,
                                   absl::Span<const uint8_t> write_secret) {
  QUIC_DVLOG(1) << ENDPOINT << "SetWriteSecret level=" << level;
  std::unique_ptr<QuicEncrypter> encrypter =
      QuicEncrypter::CreateFromCipherSuite(SSL_CIPHER_get_id(cipher));
  const EVP_MD* prf = Prf(cipher);
  CryptoUtils::SetKeyAndIV(prf, write_secret,
                           handshaker_delegate_->parsed_version(),
                           encrypter.get());
  std::vector<uint8_t> header_protection_key =
      CryptoUtils::GenerateHeaderProtectionKey(
          prf, write_secret, handshaker_delegate_->parsed_version(),
          encrypter->GetKeySize());
  encrypter->SetHeaderProtectionKey(
      absl::string_view(reinterpret_cast<char*>(header_protection_key.data()),
                        header_protection_key.size()));
  if (level == ENCRYPTION_FORWARD_SECURE) {
    QUICHE_DCHECK(latest_write_secret_.empty());
    latest_write_secret_.assign(write_secret.begin(), write_secret.end());
    one_rtt_write_header_protection_key_ = header_protection_key;
  }
  handshaker_delegate_->OnNewEncryptionKeyAvailable(level,
                                                    std::move(encrypter));
}

bool TlsHandshaker::SetReadSecret(EncryptionLevel level,
                                  const SSL_CIPHER* cipher,
                                  absl::Span<const uint8_t> read_secret) {
  QUIC_DVLOG(1) << ENDPOINT << "SetReadSecret level=" << level
                << ", connection_closed=" << is_connection_closed();
  if (check_connected_before_set_read_secret_) {
    if (is_connection_closed()) {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_check_connected_before_set_read_secret,
                                   1, 2);
      return false;
    }
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_check_connected_before_set_read_secret, 2,
                                 2);
  }
  std::unique_ptr<QuicDecrypter> decrypter =
      QuicDecrypter::CreateFromCipherSuite(SSL_CIPHER_get_id(cipher));
  const EVP_MD* prf = Prf(cipher);
  CryptoUtils::SetKeyAndIV(prf, read_secret,
                           handshaker_delegate_->parsed_version(),
                           decrypter.get());
  std::vector<uint8_t> header_protection_key =
      CryptoUtils::GenerateHeaderProtectionKey(
          prf, read_secret, handshaker_delegate_->parsed_version(),
          decrypter->GetKeySize());
  decrypter->SetHeaderProtectionKey(
      absl::string_view(reinterpret_cast<char*>(header_protection_key.data()),
                        header_protection_key.size()));
  if (level == ENCRYPTION_FORWARD_SECURE) {
    QUICHE_DCHECK(latest_read_secret_.empty());
    latest_read_secret_.assign(read_secret.begin(), read_secret.end());
    one_rtt_read_header_protection_key_ = header_protection_key;
  }
  return handshaker_delegate_->OnNewDecryptionKeyAvailable(
      level, std::move(decrypter),
      /*set_alternative_decrypter=*/false,
      /*latch_once_used=*/false);
}

std::unique_ptr<QuicDecrypter>
TlsHandshaker::AdvanceKeysAndCreateCurrentOneRttDecrypter() {
  if (latest_read_secret_.empty() || latest_write_secret_.empty() ||
      one_rtt_read_header_protection_key_.empty() ||
      one_rtt_write_header_protection_key_.empty()) {
    std::string error_details = "1-RTT secret(s) not set yet.";
    QUIC_BUG(quic_bug_10312_1) << error_details;
    CloseConnection(QUIC_INTERNAL_ERROR, error_details);
    return nullptr;
  }
  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl());
  const EVP_MD* prf = Prf(cipher);
  latest_read_secret_ = CryptoUtils::GenerateNextKeyPhaseSecret(
      prf, handshaker_delegate_->parsed_version(), latest_read_secret_);
  latest_write_secret_ = CryptoUtils::GenerateNextKeyPhaseSecret(
      prf, handshaker_delegate_->parsed_version(), latest_write_secret_);

  std::unique_ptr<QuicDecrypter> decrypter =
      QuicDecrypter::CreateFromCipherSuite(SSL_CIPHER_get_id(cipher));
  CryptoUtils::SetKeyAndIV(prf, latest_read_secret_,
                           handshaker_delegate_->parsed_version(),
                           decrypter.get());
  decrypter->SetHeaderProtectionKey(absl::string_view(
      reinterpret_cast<char*>(one_rtt_read_header_protection_key_.data()),
      one_rtt_read_header_protection_key_.size()));

  return decrypter;
}

std::unique_ptr<QuicEncrypter> TlsHandshaker::CreateCurrentOneRttEncrypter() {
  if (latest_write_secret_.empty() ||
      one_rtt_write_header_protection_key_.empty()) {
    std::string error_details = "1-RTT write secret not set yet.";
    QUIC_BUG(quic_bug_10312_2) << error_details;
    CloseConnection(QUIC_INTERNAL_ERROR, error_details);
    return nullptr;
  }
  const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl());
  std::unique_ptr<QuicEncrypter> encrypter =
      QuicEncrypter::CreateFromCipherSuite(SSL_CIPHER_get_id(cipher));
  CryptoUtils::SetKeyAndIV(Prf(cipher), latest_write_secret_,
                           handshaker_delegate_->parsed_version(),
                           encrypter.get());
  encrypter->SetHeaderProtectionKey(absl::string_view(
      reinterpret_cast<char*>(one_rtt_write_header_protection_key_.data()),
      one_rtt_write_header_protection_key_.size()));
  return encrypter;
}

bool TlsHandshaker::ExportKeyingMaterialForLabel(absl::string_view label,
                                                 absl::string_view context,
                                                 size_t result_len,
                                                 std::string* result) {
  if (result == nullptr) {
    return false;
  }
  result->resize(result_len);
  return SSL_export_keying_material(
             ssl(), reinterpret_cast<uint8_t*>(&*result->begin()), result_len,
             label.data(), label.size(),
             reinterpret_cast<const uint8_t*>(context.data()), context.size(),
             !context.empty()) == 1;
}

void TlsHandshaker::WriteMessage(EncryptionLevel level,
                                 absl::string_view data) {
  stream_->WriteCryptoData(level, data);
}

void TlsHandshaker::FlushFlight() {}

void TlsHandshaker::SendAlert(EncryptionLevel level, uint8_t desc) {
  TlsAlert tls_alert;
  tls_alert.level = level;
  tls_alert.desc = desc;
  last_tls_alert_ = tls_alert;
}

void TlsHandshaker::MessageCallback(bool is_write, int /*version*/,
                                    int content_type, absl::string_view data) {
#if BORINGSSL_API_VERSION >= 17
  if (content_type == SSL3_RT_CLIENT_HELLO_INNER) {
    // Notify QuicConnectionDebugVisitor. Most TLS messages can be seen in
    // CRYPTO frames, but, with ECH enabled, the ClientHelloInner is encrypted
    // separately.
    if (is_write) {
      handshaker_delegate_->OnEncryptedClientHelloSent(data);
    } else {
      handshaker_delegate_->OnEncryptedClientHelloReceived(data);
    }
  }
#else   // BORINGSSL_API_VERSION
  (void)is_write;
  (void)content_type;
  (void)data;
#endif  // BORINGSSL_API_VERSION
}

}  // namespace quic
