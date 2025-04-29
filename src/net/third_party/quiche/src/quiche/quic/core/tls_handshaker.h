// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_TLS_HANDSHAKER_H_
#define QUICHE_QUIC_CORE_TLS_HANDSHAKER_H_

#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/crypto_handshake.h"
#include "quiche/quic/core/crypto/crypto_message_parser.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/crypto/quic_decrypter.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"
#include "quiche/quic/core/crypto/tls_connection.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

class QuicCryptoStream;

// Base class for TlsClientHandshaker and TlsServerHandshaker. TlsHandshaker
// provides functionality common to both the client and server, such as moving
// messages between the TLS stack and the QUIC crypto stream, and handling
// derivation of secrets.
class QUICHE_EXPORT TlsHandshaker : public TlsConnection::Delegate,
                                    public CryptoMessageParser {
 public:
  // TlsHandshaker does not take ownership of any of its arguments; they must
  // outlive the TlsHandshaker.
  TlsHandshaker(QuicCryptoStream* stream, QuicSession* session);
  TlsHandshaker(const TlsHandshaker&) = delete;
  TlsHandshaker& operator=(const TlsHandshaker&) = delete;

  ~TlsHandshaker() override;

  // From CryptoMessageParser
  bool ProcessInput(absl::string_view input, EncryptionLevel level) override;
  size_t InputBytesRemaining() const override { return 0; }
  QuicErrorCode error() const override { return parser_error_; }
  const std::string& error_detail() const override {
    return parser_error_detail_;
  }
  absl::string_view extra_error_details() const { return extra_error_details_; }

  // The following methods provide implementations to subclasses of
  // TlsHandshaker which use them to implement methods of QuicCryptoStream.
  CryptoMessageParser* crypto_message_parser() { return this; }
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const;
  ssl_early_data_reason_t EarlyDataReason() const;
  std::unique_ptr<QuicDecrypter> AdvanceKeysAndCreateCurrentOneRttDecrypter();
  std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter();
  virtual HandshakeState GetHandshakeState() const = 0;
  bool ExportKeyingMaterialForLabel(absl::string_view label,
                                    absl::string_view context,
                                    size_t result_len, std::string* result);

 protected:
  // Called when a new message is received on the crypto stream and is available
  // for the TLS stack to read.
  virtual void AdvanceHandshake();

  void CloseConnection(QuicErrorCode error, const std::string& reason_phrase);
  // Closes the connection, specifying the wire error code |ietf_error|
  // explicitly.
  void CloseConnection(QuicErrorCode error,
                       QuicIetfTransportErrorCodes ietf_error,
                       const std::string& reason_phrase);

  void OnConnectionClosed(QuicErrorCode error, ConnectionCloseSource source);

  bool is_connection_closed() const { return is_connection_closed_; }

  // Called when |SSL_do_handshake| returns 1, indicating that the handshake has
  // finished. Note that a handshake only finishes once, entering early data
  // does not count.
  virtual void FinishHandshake() = 0;

  // Called when |SSL_do_handshake| returns 1 and the connection is in early
  // data. In that case, |AdvanceHandshake| will call |OnEnterEarlyData| and
  // retry |SSL_do_handshake| once.
  virtual void OnEnterEarlyData() {
    // By default, do nothing but check the preconditions.
    QUICHE_DCHECK(SSL_in_early_data(ssl()));
  }

  // Called when a handshake message is received after the handshake is
  // complete.
  virtual void ProcessPostHandshakeMessage() = 0;

  // Called when an unexpected error code is received from |SSL_get_error|. If a
  // subclass can expect more than just a single error (as provided by
  // |set_expected_ssl_error|), it can override this method to handle that case.
  virtual bool ShouldCloseConnectionOnUnexpectedError(int ssl_error);

  void set_expected_ssl_error(int ssl_error) {
    expected_ssl_error_ = ssl_error;
  }
  int expected_ssl_error() const { return expected_ssl_error_; }

  // Called to verify a cert chain. This can be implemented as a simple wrapper
  // around ProofVerifier, which optionally gathers additional arguments to pass
  // into their VerifyCertChain method. This class retains a non-owning pointer
  // to |callback|; the callback must live until this function returns
  // QUIC_SUCCESS or QUIC_FAILURE, or until the callback is run.
  //
  // If certificate verification fails, |*out_alert| may be set to a TLS alert
  // that will be sent when closing the connection; it defaults to
  // certificate_unknown. Implementations of VerifyCertChain may retain the
  // |out_alert| pointer while performing an async operation.
  virtual QuicAsyncStatus VerifyCertChain(
      const std::vector<std::string>& certs, std::string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details, uint8_t* out_alert,
      std::unique_ptr<ProofVerifierCallback> callback) = 0;
  // Called when certificate verification is completed.
  virtual void OnProofVerifyDetailsAvailable(
      const ProofVerifyDetails& verify_details) = 0;

  // Returns the PRF used by the cipher suite negotiated in the TLS handshake.
  const EVP_MD* Prf(const SSL_CIPHER* cipher);

  virtual const TlsConnection* tls_connection() const = 0;

  SSL* ssl() const { return tls_connection()->ssl(); }

  QuicCryptoStream* stream() { return stream_; }
  HandshakerDelegateInterface* handshaker_delegate() {
    return handshaker_delegate_;
  }

  enum ssl_verify_result_t VerifyCert(uint8_t* out_alert) override;

  // SetWriteSecret provides the encryption secret used to encrypt messages at
  // encryption level |level|. The secret provided here is the one from the TLS
  // 1.3 key schedule (RFC 8446 section 7.1), in particular the handshake
  // traffic secrets and application traffic secrets. The provided write secret
  // must be used with the provided cipher suite |cipher|.
  void SetWriteSecret(EncryptionLevel level, const SSL_CIPHER* cipher,
                      absl::Span<const uint8_t> write_secret) override;

  // SetReadSecret is similar to SetWriteSecret, except that it is used for
  // decrypting messages. SetReadSecret at a particular level is always called
  // after SetWriteSecret for that level, except for ENCRYPTION_ZERO_RTT, where
  // the EncryptionLevel for SetWriteSecret is ENCRYPTION_FORWARD_SECURE.
  bool SetReadSecret(EncryptionLevel level, const SSL_CIPHER* cipher,
                     absl::Span<const uint8_t> read_secret) override;

  // WriteMessage is called when there is |data| from the TLS stack ready for
  // the QUIC stack to write in a crypto frame. The data must be transmitted at
  // encryption level |level|.
  void WriteMessage(EncryptionLevel level, absl::string_view data) override;

  // FlushFlight is called to signal that the current flight of
  // messages have all been written (via calls to WriteMessage) and can be
  // flushed to the underlying transport.
  void FlushFlight() override;

  // SendAlert causes this TlsHandshaker to close the QUIC connection with an
  // error code corresponding to the TLS alert description |desc|.
  void SendAlert(EncryptionLevel level, uint8_t desc) override;

  // Informational callback from BoringSSL. Subclasses can override it to do
  // logging, tracing, etc.
  // See |SSL_CTX_set_info_callback| for the meaning of |type| and |value|.
  void InfoCallback(int /*type*/, int /*value*/) override {}

  // Message callback from BoringSSL, for debugging purposes. See
  // |SSL_CTX_set_msg_callback| for how to interpret |version|, |content_type|,
  // and |data|.
  void MessageCallback(bool is_write, int version, int content_type,
                       absl::string_view data) override;

  void set_extra_error_details(std::string extra_error_details) {
    extra_error_details_ = std::move(extra_error_details);
  }

 private:
  // ProofVerifierCallbackImpl handles the result of an asynchronous certificate
  // verification operation.
  class QUICHE_EXPORT ProofVerifierCallbackImpl : public ProofVerifierCallback {
   public:
    explicit ProofVerifierCallbackImpl(TlsHandshaker* parent);
    ~ProofVerifierCallbackImpl() override;

    // ProofVerifierCallback interface.
    void Run(bool ok, const std::string& error_details,
             std::unique_ptr<ProofVerifyDetails>* details) override;

    // If called, Cancel causes the pending callback to be a no-op.
    void Cancel();

   private:
    // Non-owning pointer to the TlsHandshaker responsible for this callback.
    // |parent_| must be valid for the life of this callback or until |Cancel|
    // is called.
    TlsHandshaker* parent_;
  };

  // ProofVerifierCallback used for async certificate verification. Ownership of
  // this object is transferred to |VerifyCertChain|;
  ProofVerifierCallbackImpl* proof_verify_callback_ = nullptr;
  std::unique_ptr<ProofVerifyDetails> verify_details_;
  enum ssl_verify_result_t verify_result_ = ssl_verify_retry;
  uint8_t cert_verify_tls_alert_ = SSL_AD_CERTIFICATE_UNKNOWN;
  std::string cert_verify_error_details_;

  int expected_ssl_error_ = SSL_ERROR_WANT_READ;
  bool is_connection_closed_ = false;

  QuicCryptoStream* stream_;
  HandshakerDelegateInterface* handshaker_delegate_;

  QuicErrorCode parser_error_ = QUIC_NO_ERROR;
  std::string parser_error_detail_;

  // Arbitrary error string that will be added to the connection close error
  // details when TlsHandshaker::CloseConnection is called.
  std::string extra_error_details_;

  // The most recently derived 1-RTT read and write secrets, which are updated
  // on each key update.
  std::vector<uint8_t> latest_read_secret_;
  std::vector<uint8_t> latest_write_secret_;
  // 1-RTT header protection keys, which are not changed during key update.
  std::vector<uint8_t> one_rtt_read_header_protection_key_;
  std::vector<uint8_t> one_rtt_write_header_protection_key_;

  struct TlsAlert {
    EncryptionLevel level;
    // The TLS alert code as listed in
    // https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-6
    uint8_t desc;
  };
  std::optional<TlsAlert> last_tls_alert_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_TLS_HANDSHAKER_H_
