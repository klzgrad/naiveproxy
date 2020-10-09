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
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

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
  TlsHandshaker(QuicCryptoStream* stream, QuicSession* session);
  TlsHandshaker(const TlsHandshaker&) = delete;
  TlsHandshaker& operator=(const TlsHandshaker&) = delete;

  ~TlsHandshaker() override;

  // From CryptoMessageParser
  bool ProcessInput(quiche::QuicheStringPiece input,
                    EncryptionLevel level) override;
  size_t InputBytesRemaining() const override { return 0; }
  QuicErrorCode error() const override { return parser_error_; }
  const std::string& error_detail() const override {
    return parser_error_detail_;
  }

  // From QuicCryptoStream
  virtual bool encryption_established() const = 0;
  virtual bool one_rtt_keys_available() const = 0;
  virtual const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const = 0;
  virtual CryptoMessageParser* crypto_message_parser() { return this; }
  virtual HandshakeState GetHandshakeState() const = 0;
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const;

 protected:
  virtual void AdvanceHandshake() = 0;

  virtual void CloseConnection(QuicErrorCode error,
                               const std::string& reason_phrase) = 0;

  // Returns the PRF used by the cipher suite negotiated in the TLS handshake.
  const EVP_MD* Prf(const SSL_CIPHER* cipher);

  virtual const TlsConnection* tls_connection() const = 0;

  SSL* ssl() const { return tls_connection()->ssl(); }

  QuicCryptoStream* stream() { return stream_; }
  HandshakerDelegateInterface* handshaker_delegate() {
    return handshaker_delegate_;
  }

  // SetWriteSecret provides the encryption secret used to encrypt messages at
  // encryption level |level|. The secret provided here is the one from the TLS
  // 1.3 key schedule (RFC 8446 section 7.1), in particular the handshake
  // traffic secrets and application traffic secrets. The provided write secret
  // must be used with the provided cipher suite |cipher|.
  void SetWriteSecret(EncryptionLevel level,
                      const SSL_CIPHER* cipher,
                      const std::vector<uint8_t>& write_secret) override;

  // SetReadSecret is similar to SetWriteSecret, except that it is used for
  // decrypting messages. SetReadSecret at a particular level is always called
  // after SetWriteSecret for that level, except for ENCRYPTION_ZERO_RTT, where
  // the EncryptionLevel for SetWriteSecret is ENCRYPTION_FORWARD_SECURE.
  bool SetReadSecret(EncryptionLevel level,
                     const SSL_CIPHER* cipher,
                     const std::vector<uint8_t>& read_secret) override;

  // WriteMessage is called when there is |data| from the TLS stack ready for
  // the QUIC stack to write in a crypto frame. The data must be transmitted at
  // encryption level |level|.
  void WriteMessage(EncryptionLevel level,
                    quiche::QuicheStringPiece data) override;

  // FlushFlight is called to signal that the current flight of
  // messages have all been written (via calls to WriteMessage) and can be
  // flushed to the underlying transport.
  void FlushFlight() override;

  // SendAlert causes this TlsHandshaker to close the QUIC connection with an
  // error code corresponding to the TLS alert description |desc|.
  void SendAlert(EncryptionLevel level, uint8_t desc) override;

 private:
  QuicCryptoStream* stream_;
  HandshakerDelegateInterface* handshaker_delegate_;

  QuicErrorCode parser_error_ = QUIC_NO_ERROR;
  std::string parser_error_detail_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_TLS_HANDSHAKER_H_
