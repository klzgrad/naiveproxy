// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_TLS_HANDSHAKER_H_
#define NET_THIRD_PARTY_QUIC_CORE_TLS_HANDSHAKER_H_

#include "net/third_party/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quic/core/crypto/crypto_message_parser.h"
#include "net/third_party/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quic/core/quic_session.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "third_party/boringssl/src/include/openssl/base.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

namespace quic {

class QuicCryptoStream;

// Base class for TlsClientHandshaker and TlsServerHandshaker. TlsHandshaker
// provides functionality common to both the client and server, such as moving
// messages between the TLS stack and the QUIC crypto stream, and handling
// derivation of secrets.
class QUIC_EXPORT_PRIVATE TlsHandshaker : public CryptoMessageParser {
 public:
  // TlsHandshaker does not take ownership of any of its arguments; they must
  // outlive the TlsHandshaker.
  TlsHandshaker(QuicCryptoStream* stream,
                QuicSession* session,
                SSL_CTX* ssl_ctx);
  TlsHandshaker(const TlsHandshaker&) = delete;
  TlsHandshaker& operator=(const TlsHandshaker&) = delete;

  ~TlsHandshaker() override;

  // From CryptoMessageParser
  bool ProcessInput(QuicStringPiece input, EncryptionLevel level) override;
  size_t InputBytesRemaining() const override { return 0; }
  QuicErrorCode error() const override { return parser_error_; }
  const QuicString& error_detail() const override {
    return parser_error_detail_;
  };

  // From QuicCryptoStream
  virtual QuicLongHeaderType GetLongHeaderType(
      QuicStreamOffset offset) const = 0;
  virtual bool encryption_established() const = 0;
  virtual bool handshake_confirmed() const = 0;
  virtual const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const = 0;
  virtual CryptoMessageParser* crypto_message_parser() { return this; }

 protected:
  virtual void AdvanceHandshake() = 0;

  virtual void CloseConnection(QuicErrorCode error,
                               const QuicString& reason_phrase) = 0;

  // Creates an SSL_CTX and configures it with the options that are appropriate
  // for both client and server. The caller is responsible for ownership of the
  // newly created struct.
  static bssl::UniquePtr<SSL_CTX> CreateSslCtx();

  // From a given SSL* |ssl|, returns a pointer to the TlsHandshaker that it
  // belongs to. This is a helper method for implementing callbacks set on an
  // SSL, as it allows the callback function to find the TlsHandshaker instance
  // and call an instance method.
  static TlsHandshaker* HandshakerFromSsl(const SSL* ssl);

  static EncryptionLevel QuicEncryptionLevel(enum ssl_encryption_level_t level);
  static enum ssl_encryption_level_t BoringEncryptionLevel(
      EncryptionLevel level);

  // Returns the PRF used by the cipher suite negotiated in the TLS handshake.
  const EVP_MD* Prf();

  std::unique_ptr<QuicEncrypter> CreateEncrypter(
      const std::vector<uint8_t>& pp_secret);
  std::unique_ptr<QuicDecrypter> CreateDecrypter(
      const std::vector<uint8_t>& pp_secret);

  SSL* ssl() { return ssl_.get(); }
  QuicCryptoStream* stream() { return stream_; }
  QuicSession* session() { return session_; }

 private:
  // TlsHandshaker implements SSL_QUIC_METHOD, which provides the interface
  // between BoringSSL's TLS stack and a QUIC implementation.
  static const SSL_QUIC_METHOD kSslQuicMethod;

  // Pointers to the following 4 functions form |kSslQuicMethod|. Each one
  // is a wrapper around the corresponding instance method below. The |ssl|
  // argument is used to look up correct TlsHandshaker instance on which to call
  // the method. According to the BoringSSL documentation, these functions
  // return 0 on error and 1 otherwise; here they never error and thus always
  // return 1.
  static int SetEncryptionSecretCallback(SSL* ssl,
                                         enum ssl_encryption_level_t level,
                                         const uint8_t* read_key,
                                         const uint8_t* write_key,
                                         size_t secret_len);
  static int WriteMessageCallback(SSL* ssl,
                                  enum ssl_encryption_level_t level,
                                  const uint8_t* data,
                                  size_t len);
  static int FlushFlightCallback(SSL* ssl);
  static int SendAlertCallback(SSL* ssl,
                               enum ssl_encryption_level_t level,
                               uint8_t alert);

  // SetEncryptionSecret provides the encryption secret to use at a particular
  // encryption level. The secrets provided here are the ones from the TLS 1.3
  // key schedule (RFC 8446 section 7.1), in particular the handshake traffic
  // secrets and application traffic secrets. For a given secret |secret|,
  // |level| indicates which EncryptionLevel it is to be used at, and |is_write|
  // indicates whether it is used for encryption or decryption.
  void SetEncryptionSecret(EncryptionLevel level,
                           const std::vector<uint8_t>& read_secret,
                           const std::vector<uint8_t>& write_secret);

  // WriteMessage is called when there is |data| from the TLS stack ready for
  // the QUIC stack to write in a crypto frame. The data must be transmitted at
  // encryption level |level|.
  void WriteMessage(EncryptionLevel level, QuicStringPiece data);

  // FlushFlight is called to signal that the current flight of
  // messages have all been written (via calls to WriteMessage) and can be
  // flushed to the underlying transport.
  void FlushFlight();

  // SendAlert causes this TlsHandshaker to close the QUIC connection with an
  // error code corresponding to the TLS alert description |desc|.
  void SendAlert(EncryptionLevel level, uint8_t desc);

  QuicCryptoStream* stream_;
  QuicSession* session_;

  QuicErrorCode parser_error_ = QUIC_NO_ERROR;
  QuicString parser_error_detail_;

  bssl::UniquePtr<SSL> ssl_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_TLS_HANDSHAKER_H_
