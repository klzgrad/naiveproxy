// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_TLS_HANDSHAKER_H_
#define QUICHE_QUIC_CORE_TLS_HANDSHAKER_H_

#include "third_party/boringssl/src/include/openssl/base.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_message_parser.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/tls_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class QuicCryptoStream;

// Base class for TlsClientHandshaker and TlsServerHandshaker. TlsHandshaker
// provides functionality common to both the client and server, such as moving
// messages between the TLS stack and the QUIC crypto stream, and handling
// derivation of secrets.
class QUIC_EXPORT_PRIVATE TlsHandshaker : public TlsConnection::Delegate,
                                          public CryptoMessageParser {
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
  const std::string& error_detail() const override {
    return parser_error_detail_;
  }

  // From QuicCryptoStream
  virtual bool encryption_established() const = 0;
  virtual bool handshake_confirmed() const = 0;
  virtual const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const = 0;
  virtual CryptoMessageParser* crypto_message_parser() { return this; }
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const;

 protected:
  virtual void AdvanceHandshake() = 0;

  virtual void CloseConnection(QuicErrorCode error,
                               const std::string& reason_phrase) = 0;

  // Returns the PRF used by the cipher suite negotiated in the TLS handshake.
  const EVP_MD* Prf();

  std::unique_ptr<QuicEncrypter> CreateEncrypter(
      const std::vector<uint8_t>& pp_secret);
  std::unique_ptr<QuicDecrypter> CreateDecrypter(
      const std::vector<uint8_t>& pp_secret);

  virtual const TlsConnection* tls_connection() const = 0;

  SSL* ssl() const { return tls_connection()->ssl(); }

  QuicCryptoStream* stream() { return stream_; }
  QuicSession* session() { return session_; }

  // SetEncryptionSecret provides the encryption secret to use at a particular
  // encryption level. The secrets provided here are the ones from the TLS 1.3
  // key schedule (RFC 8446 section 7.1), in particular the handshake traffic
  // secrets and application traffic secrets. For a given secret |secret|,
  // |level| indicates which EncryptionLevel it is to be used at, and |is_write|
  // indicates whether it is used for encryption or decryption.
  void SetEncryptionSecret(EncryptionLevel level,
                           const std::vector<uint8_t>& read_secret,
                           const std::vector<uint8_t>& write_secret) override;

  // WriteMessage is called when there is |data| from the TLS stack ready for
  // the QUIC stack to write in a crypto frame. The data must be transmitted at
  // encryption level |level|.
  void WriteMessage(EncryptionLevel level, QuicStringPiece data) override;

  // FlushFlight is called to signal that the current flight of
  // messages have all been written (via calls to WriteMessage) and can be
  // flushed to the underlying transport.
  void FlushFlight() override;

  // SendAlert causes this TlsHandshaker to close the QUIC connection with an
  // error code corresponding to the TLS alert description |desc|.
  void SendAlert(EncryptionLevel level, uint8_t desc) override;

 private:
  QuicCryptoStream* stream_;
  QuicSession* session_;

  QuicErrorCode parser_error_ = QUIC_NO_ERROR;
  std::string parser_error_detail_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_TLS_HANDSHAKER_H_
