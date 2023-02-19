// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_TLS_CONNECTION_H_
#define QUICHE_QUIC_CORE_CRYPTO_TLS_CONNECTION_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/quic_types.h"

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
  class QUIC_EXPORT_PRIVATE Delegate {
   public:
    virtual ~Delegate() {}

   protected:
    // Certificate management functions:

    // Verifies the peer's certificate chain. It may use
    // SSL_get0_peer_certificates to get the cert chain. This method returns
    // ssl_verify_ok if the cert is valid, ssl_verify_invalid if it is invalid,
    // or ssl_verify_retry if verification is happening asynchronously.
    virtual enum ssl_verify_result_t VerifyCert(uint8_t* out_alert) = 0;

    // QUIC-TLS interface functions:

    // SetWriteSecret provides the encryption secret used to encrypt messages at
    // encryption level |level|. The secret provided here is the one from the
    // TLS 1.3 key schedule (RFC 8446 section 7.1), in particular the handshake
    // traffic secrets and application traffic secrets. The provided write
    // secret must be used with the provided cipher suite |cipher|.
    virtual void SetWriteSecret(EncryptionLevel level, const SSL_CIPHER* cipher,
                                absl::Span<const uint8_t> write_secret) = 0;

    // SetReadSecret is similar to SetWriteSecret, except that it is used for
    // decrypting messages. SetReadSecret at a particular level is always called
    // after SetWriteSecret for that level, except for ENCRYPTION_ZERO_RTT,
    // where the EncryptionLevel for SetWriteSecret is
    // ENCRYPTION_FORWARD_SECURE.
    virtual bool SetReadSecret(EncryptionLevel level, const SSL_CIPHER* cipher,
                               absl::Span<const uint8_t> read_secret) = 0;

    // WriteMessage is called when there is |data| from the TLS stack ready for
    // the QUIC stack to write in a crypto frame. The data must be transmitted
    // at encryption level |level|.
    virtual void WriteMessage(EncryptionLevel level,
                              absl::string_view data) = 0;

    // FlushFlight is called to signal that the current flight of messages have
    // all been written (via calls to WriteMessage) and can be flushed to the
    // underlying transport.
    virtual void FlushFlight() = 0;

    // SendAlert causes this TlsConnection to close the QUIC connection with an
    // error code corersponding to the TLS alert description |desc| sent at
    // level |level|.
    virtual void SendAlert(EncryptionLevel level, uint8_t desc) = 0;

    // Informational callback from BoringSSL. This callback is disabled by
    // default, but can be enabled by TlsConnection::EnableInfoCallback.
    //
    // See |SSL_CTX_set_info_callback| for the meaning of |type| and |value|.
    virtual void InfoCallback(int type, int value) = 0;

    friend class TlsConnection;
  };

  TlsConnection(const TlsConnection&) = delete;
  TlsConnection& operator=(const TlsConnection&) = delete;

  // Configure the SSL such that delegate_->InfoCallback will be called.
  void EnableInfoCallback();

  // Configure the SSL to disable session ticket support. Note that, this
  // function simply sets the |SSL_OP_NO_TICKET| option on the SSL object, it
  // does not check whether it is too late to do so.
  void DisableTicketSupport();

  // Functions to convert between BoringSSL's enum ssl_encryption_level_t and
  // QUIC's EncryptionLevel.
  static EncryptionLevel QuicEncryptionLevel(enum ssl_encryption_level_t level);
  static enum ssl_encryption_level_t BoringEncryptionLevel(
      EncryptionLevel level);

  SSL* ssl() const { return ssl_.get(); }

  const QuicSSLConfig& ssl_config() const { return ssl_config_; }

 protected:
  // TlsConnection does not take ownership of |ssl_ctx| or |delegate|; they must
  // outlive the TlsConnection object.
  TlsConnection(SSL_CTX* ssl_ctx, Delegate* delegate, QuicSSLConfig ssl_config);

  // Creates an SSL_CTX and configures it with the options that are appropriate
  // for both client and server. The caller is responsible for ownership of the
  // newly created struct.
  static bssl::UniquePtr<SSL_CTX> CreateSslCtx();

  // From a given SSL* |ssl|, returns a pointer to the TlsConnection that it
  // belongs to. This helper method allows the callbacks set in BoringSSL to be
  // dispatched to the correct TlsConnection from the SSL* passed into the
  // callback.
  static TlsConnection* ConnectionFromSsl(const SSL* ssl);

  // Registered as the callback for SSL(_CTX)_set_custom_verify. The
  // implementation is delegated to Delegate::VerifyCert.
  static enum ssl_verify_result_t VerifyCallback(SSL* ssl, uint8_t* out_alert);

  QuicSSLConfig& mutable_ssl_config() { return ssl_config_; }

 private:
  // TlsConnection implements SSL_QUIC_METHOD, which provides the interface
  // between BoringSSL's TLS stack and a QUIC implementation.
  static const SSL_QUIC_METHOD kSslQuicMethod;

  // The following static functions make up the members of kSslQuicMethod:
  static int SetReadSecretCallback(SSL* ssl, enum ssl_encryption_level_t level,
                                   const SSL_CIPHER* cipher,
                                   const uint8_t* secret, size_t secret_len);
  static int SetWriteSecretCallback(SSL* ssl, enum ssl_encryption_level_t level,
                                    const SSL_CIPHER* cipher,
                                    const uint8_t* secret, size_t secret_len);
  static int WriteMessageCallback(SSL* ssl, enum ssl_encryption_level_t level,
                                  const uint8_t* data, size_t len);
  static int FlushFlightCallback(SSL* ssl);
  static int SendAlertCallback(SSL* ssl, enum ssl_encryption_level_t level,
                               uint8_t desc);

  Delegate* delegate_;
  bssl::UniquePtr<SSL> ssl_;
  QuicSSLConfig ssl_config_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_TLS_CONNECTION_H_
