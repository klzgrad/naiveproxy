// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_TLS_SERVER_CONNECTION_H_
#define QUICHE_QUIC_CORE_CRYPTO_TLS_SERVER_CONNECTION_H_

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/crypto/tls_connection.h"

namespace quic {

// TlsServerConnection receives calls for client-specific BoringSSL callbacks
// and calls its Delegate for the implementation of those callbacks.
class QUICHE_EXPORT TlsServerConnection : public TlsConnection {
 public:
  // A TlsServerConnection::Delegate implement the server-specific methods that
  // are set as callbacks for an SSL object.
  class QUICHE_EXPORT Delegate {
   public:
    virtual ~Delegate() {}

   protected:
    // Called from BoringSSL right after SNI is extracted, which is very early
    // in the handshake process.
    virtual ssl_select_cert_result_t EarlySelectCertCallback(
        const SSL_CLIENT_HELLO* client_hello) = 0;

    // Called after the ClientHello extensions have been successfully parsed.
    // Returns an SSL_TLSEXT_ERR_* value (see
    // https://commondatastorage.googleapis.com/chromium-boringssl-docs/ssl.h.html#SSL_CTX_set_tlsext_servername_callback).
    //
    // On success, return SSL_TLSEXT_ERR_OK causes the server_name extension to
    // be acknowledged in the ServerHello, or return SSL_TLSEXT_ERR_NOACK which
    // causes it to be not acknowledged.
    //
    // If the function returns SSL_TLSEXT_ERR_ALERT_FATAL, then it puts in
    // |*out_alert| the TLS alert value that the server will send.
    //
    virtual int TlsExtServernameCallback(int* out_alert) = 0;

    // Selects which ALPN to use based on the list sent by the client.
    virtual int SelectAlpn(const uint8_t** out, uint8_t* out_len,
                           const uint8_t* in, unsigned in_len) = 0;

    // Signs |in| using the signature algorithm specified by |sig_alg| (an
    // SSL_SIGN_* value). If the signing operation cannot be completed
    // synchronously, ssl_private_key_retry is returned. If there is an error
    // signing, or if the signature is longer than |max_out|, then
    // ssl_private_key_failure is returned. Otherwise, ssl_private_key_success
    // is returned with the signature put in |*out| and the length in
    // |*out_len|.
    virtual ssl_private_key_result_t PrivateKeySign(uint8_t* out,
                                                    size_t* out_len,
                                                    size_t max_out,
                                                    uint16_t sig_alg,
                                                    absl::string_view in) = 0;

    // When PrivateKeySign returns ssl_private_key_retry, PrivateKeyComplete
    // will be called after the async sign operation has completed.
    // PrivateKeyComplete puts the resulting signature in |*out| and length in
    // |*out_len|. If the length is greater than |max_out| or if there was an
    // error in signing, then ssl_private_key_failure is returned. Otherwise,
    // ssl_private_key_success is returned.
    virtual ssl_private_key_result_t PrivateKeyComplete(uint8_t* out,
                                                        size_t* out_len,
                                                        size_t max_out) = 0;

    // The following functions are used to implement an SSL_TICKET_AEAD_METHOD.
    // See
    // https://commondatastorage.googleapis.com/chromium-boringssl-docs/ssl.h.html#ssl_ticket_aead_result_t
    // for details on the BoringSSL API.

    // SessionTicketMaxOverhead returns the maximum number of bytes of overhead
    // that SessionTicketSeal may add when encrypting a session ticket.
    virtual size_t SessionTicketMaxOverhead() = 0;

    // SessionTicketSeal encrypts the session ticket in |in|, putting the
    // resulting encrypted ticket in |out|, writing the length of the bytes
    // written to |*out_len|, which is no larger than |max_out_len|. It returns
    // 1 on success and 0 on error.
    virtual int SessionTicketSeal(uint8_t* out, size_t* out_len,
                                  size_t max_out_len, absl::string_view in) = 0;

    // SessionTicketOpen is called when BoringSSL has an encrypted session
    // ticket |in| and wants the ticket decrypted. This decryption operation can
    // happen synchronously or asynchronously.
    //
    // If the decrypted ticket is not available at the time of the function
    // call, this function returns ssl_ticket_aead_retry. If this function
    // returns ssl_ticket_aead_retry, then SSL_do_handshake will return
    // SSL_ERROR_PENDING_TICKET. Once the pending ticket decryption has
    // completed, SSL_do_handshake needs to be called again.
    //
    // When this function is called and the decrypted ticket is available
    // (either the ticket was decrypted synchronously, or an asynchronous
    // operation has completed and SSL_do_handshake has been called again), the
    // decrypted ticket is put in |out|, and the length of that output is
    // written to |*out_len|, not to exceed |max_out_len|, and
    // ssl_ticket_aead_success is returned. If the ticket cannot be decrypted
    // and should be ignored, this function returns
    // ssl_ticket_aead_ignore_ticket and a full handshake will be performed
    // instead. If a fatal error occurs, ssl_ticket_aead_error can be returned
    // which will terminate the handshake.
    virtual enum ssl_ticket_aead_result_t SessionTicketOpen(
        uint8_t* out, size_t* out_len, size_t max_out_len,
        absl::string_view in) = 0;

    // Provides the delegate for callbacks that are shared between client and
    // server.
    virtual TlsConnection::Delegate* ConnectionDelegate() = 0;

    friend class TlsServerConnection;
  };

  TlsServerConnection(SSL_CTX* ssl_ctx, Delegate* delegate,
                      QuicSSLConfig ssl_config);

  // Creates and configures an SSL_CTX that is appropriate for servers to use.
  static bssl::UniquePtr<SSL_CTX> CreateSslCtx(ProofSource* proof_source);

  void SetCertChain(const std::vector<CRYPTO_BUFFER*>& cert_chain);

  // Set the client cert mode to be used on this connection. This should be
  // called right after cert selection at the latest, otherwise it is too late
  // to has an effect.
  void SetClientCertMode(ClientCertMode client_cert_mode);

 private:
  // Specialization of TlsConnection::ConnectionFromSsl.
  static TlsServerConnection* ConnectionFromSsl(SSL* ssl);

  static ssl_select_cert_result_t EarlySelectCertCallback(
      const SSL_CLIENT_HELLO* client_hello);

  // These functions are registered as callbacks in BoringSSL and delegate their
  // implementation to the matching methods in Delegate above.
  static int TlsExtServernameCallback(SSL* ssl, int* out_alert, void* arg);
  static int SelectAlpnCallback(SSL* ssl, const uint8_t** out, uint8_t* out_len,
                                const uint8_t* in, unsigned in_len, void* arg);

  // |kPrivateKeyMethod| is a vtable pointing to PrivateKeySign and
  // PrivateKeyComplete used by the TLS stack to compute the signature for the
  // CertificateVerify message (using the server's private key).
  static const SSL_PRIVATE_KEY_METHOD kPrivateKeyMethod;

  // The following functions make up the contents of |kPrivateKeyMethod|.
  static ssl_private_key_result_t PrivateKeySign(
      SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out, uint16_t sig_alg,
      const uint8_t* in, size_t in_len);
  static ssl_private_key_result_t PrivateKeyComplete(SSL* ssl, uint8_t* out,
                                                     size_t* out_len,
                                                     size_t max_out);

  // Implementation of SSL_TICKET_AEAD_METHOD which delegates to corresponding
  // methods in TlsServerConnection::Delegate (a.k.a. TlsServerHandshaker).
  static const SSL_TICKET_AEAD_METHOD kSessionTicketMethod;

  // The following functions make up the contents of |kSessionTicketMethod|.
  static size_t SessionTicketMaxOverhead(SSL* ssl);
  static int SessionTicketSeal(SSL* ssl, uint8_t* out, size_t* out_len,
                               size_t max_out_len, const uint8_t* in,
                               size_t in_len);
  static enum ssl_ticket_aead_result_t SessionTicketOpen(SSL* ssl, uint8_t* out,
                                                         size_t* out_len,
                                                         size_t max_out_len,
                                                         const uint8_t* in,
                                                         size_t in_len);

  // Install custom verify callback on ssl() if |ssl_config().client_cert_mode|
  // is not ClientCertMode::kNone. Uninstall otherwise.
  void UpdateCertVerifyCallback();

  Delegate* delegate_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_TLS_SERVER_CONNECTION_H_
