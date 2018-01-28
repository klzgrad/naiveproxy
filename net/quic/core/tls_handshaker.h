// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_TLS_HANDSHAKER_H_
#define NET_QUIC_CORE_TLS_HANDSHAKER_H_

#include "net/quic/core/crypto/crypto_handshake.h"
#include "net/quic/core/crypto/quic_tls_adapter.h"
#include "net/quic/platform/api/quic_export.h"
#include "third_party/boringssl/src/include/openssl/base.h"

namespace net {

class QuicCryptoStream;
class QuicSession;

// Base class for TlsClientHandshaker and TlsServerHandshaker. TlsHandshaker
// provides functionality common to both the client and server, such as moving
// messages between the TLS stack and the QUIC crypto stream, and handling
// derivation of secrets.
class QUIC_EXPORT_PRIVATE TlsHandshaker : public QuicTlsAdapter::Visitor {
 public:
  // TlsHandshaker does not take ownership of any of its arguments; they must
  // outlive the TlsHandshaker.
  TlsHandshaker(QuicCryptoStream* stream,
                QuicSession* session,
                SSL_CTX* ssl_ctx);

  ~TlsHandshaker() override;

  // Computes the 1-RTT secrets client_pp_secret_0 and server_pp_secret_0 from
  // which the packet protection keys are derived, as defined in
  // draft-ietf-quic-tls section 5.2.2. Returns true on success and false if
  // |ssl| is not in a state to export secrets.
  static bool DeriveSecrets(SSL* ssl,
                            std::vector<uint8_t>* client_secret_out,
                            std::vector<uint8_t>* server_secret_out);

  // From QuicTlsAdapter::Visitor
  void OnDataAvailableForBIO() override;
  void OnDataReceivedFromBIO(const QuicStringPiece& data) override;

  // From QuicCryptoStream
  virtual bool encryption_established() const = 0;
  virtual bool handshake_confirmed() const = 0;
  virtual const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const = 0;
  virtual CryptoMessageParser* crypto_message_parser();

 protected:
  virtual void AdvanceHandshake() = 0;

  // From a given SSL* |ssl|, returns a pointer to the TlsHandshaker that it
  // belongs to. This is a helper method for implementing callbacks set on an
  // SSL, as it allows the callback function to find the TlsHandshaker instance
  // and call an instance method.
  static TlsHandshaker* HandshakerFromSsl(const SSL* ssl);

  SSL* ssl() { return ssl_.get(); }
  QuicCryptoStream* stream() { return stream_; }
  QuicSession* session() { return session_; }

 private:
  QuicCryptoStream* stream_;
  QuicSession* session_;
  QuicTlsAdapter bio_adapter_;
  bssl::UniquePtr<SSL> ssl_;

  DISALLOW_COPY_AND_ASSIGN(TlsHandshaker);
};

}  // namespace net

#endif  // NET_QUIC_CORE_TLS_HANDSHAKER_H_
