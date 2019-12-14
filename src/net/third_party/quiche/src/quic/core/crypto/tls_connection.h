// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_TLS_CONNECTION_H_
#define QUICHE_QUIC_CORE_CRYPTO_TLS_CONNECTION_H_

#include <vector>

#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

namespace quic {

// TlsConnection wraps BoringSSL's SSL object which represents a single TLS
// connection. Callbacks set in BoringSSL which are called with an SSL* argument
// will get dispatched to the TlsConnection object owning that SSL. In turn, the
// TlsConnection will delegate the implementation of that callback to its
// Delegate.
//
// The owner of the TlsConnection is responsible for driving the TLS handshake
// (and other interactions with the SSL*). This class only handles mapping
// callbacks to the correct instance.
class QUIC_EXPORT_PRIVATE TlsConnection {
 public:
  // A TlsConnection::Delegate implements the methods that are set as callbacks
  // of TlsConnection.
  class Delegate {
   public:
    virtual ~Delegate() {}

   protected:
    // SetEncryptionSecret provides the encryption secrets to use at a
    // particular encryption level |level|. The secrets provided here are the
    // ones from the TLS 1.3 key schedule (RFC 8446 section 7.1), in particular
    // the handshake traffic secrets and application traffic secrets. For a
    // given level |level|, |read_secret| is the secret used for reading data,
    // and |write_secret| is the secret used for writing data.
    virtual void SetEncryptionSecret(
        EncryptionLevel level,
        const std::vector<uint8_t>& read_secret,
        const std::vector<uint8_t>& write_secret) = 0;

    // WriteMessage is called when there is |data| from the TLS stack ready for
    // the QUIC stack to write in a crypto frame. The data must be transmitted
    // at encryption level |level|.
    virtual void WriteMessage(EncryptionLevel level, QuicStringPiece data) = 0;

    // FlushFlight is called to signal that the current flight of messages have
    // all been written (via calls to WriteMessage) and can be flushed to the
    // underlying transport.
    virtual void FlushFlight() = 0;

    // SendAlert causes this TlsConnection to close the QUIC connection with an
    // error code corersponding to the TLS alert description |desc| sent at
    // level |level|.
    virtual void SendAlert(EncryptionLevel level, uint8_t desc) = 0;

    friend class TlsConnection;
  };

  TlsConnection(const TlsConnection&) = delete;
  TlsConnection& operator=(const TlsConnection&) = delete;

  // Functions to convert between BoringSSL's enum ssl_encryption_level_t and
  // QUIC's EncryptionLevel.
  static EncryptionLevel QuicEncryptionLevel(enum ssl_encryption_level_t level);
  static enum ssl_encryption_level_t BoringEncryptionLevel(
      EncryptionLevel level);

  SSL* ssl() const { return ssl_.get(); }

 protected:
  // TlsConnection does not take ownership of any of its arguments; they must
  // outlive the TlsConnection object.
  TlsConnection(SSL_CTX* ssl_ctx, Delegate* delegate);

  // Creates an SSL_CTX and configures it with the options that are appropriate
  // for both client and server. The caller is responsible for ownership of the
  // newly created struct.
  static bssl::UniquePtr<SSL_CTX> CreateSslCtx();

  // From a given SSL* |ssl|, returns a pointer to the TlsConnection that it
  // belongs to. This helper method allows the callbacks set in BoringSSL to be
  // dispatched to the correct TlsConnection from the SSL* passed into the
  // callback.
  static TlsConnection* ConnectionFromSsl(const SSL* ssl);

 private:
  // TlsConnection implements SSL_QUIC_METHOD, which provides the interface
  // between BoringSSL's TLS stack and a QUIC implementation.
  static const SSL_QUIC_METHOD kSslQuicMethod;

  // The following static functions make up the members of kSslQuicMethod:
  static int SetEncryptionSecretCallback(SSL* ssl,
                                         enum ssl_encryption_level_t level,
                                         const uint8_t* read_key,
                                         const uint8_t* write_key,
                                         size_t key_length);
  static int WriteMessageCallback(SSL* ssl,
                                  enum ssl_encryption_level_t level,
                                  const uint8_t* data,
                                  size_t len);
  static int FlushFlightCallback(SSL* ssl);
  static int SendAlertCallback(SSL* ssl,
                               enum ssl_encryption_level_t level,
                               uint8_t desc);

  Delegate* delegate_;
  bssl::UniquePtr<SSL> ssl_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_TLS_CONNECTION_H_
