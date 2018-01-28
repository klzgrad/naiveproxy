// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_MOCK_CRYPTO_CLIENT_STREAM_H_
#define NET_QUIC_TEST_TOOLS_MOCK_CRYPTO_CLIENT_STREAM_H_

#include <string>

#include "base/macros.h"
#include "net/quic/chromium/crypto/proof_verifier_chromium.h"
#include "net/quic/core/crypto/crypto_handshake.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/quic_crypto_client_stream.h"
#include "net/quic/core/quic_server_id.h"
#include "net/quic/core/quic_session.h"
#include "net/quic/core/quic_spdy_client_session_base.h"

namespace net {

class MockCryptoClientStream : public QuicCryptoClientStream,
                               public QuicCryptoHandshaker {
 public:
  // TODO(zhongyi): might consider move HandshakeMode up to
  // MockCryptoClientStreamFactory.
  // HandshakeMode enumerates the handshake mode MockCryptoClientStream should
  // mock in CryptoConnect.
  enum HandshakeMode {
    // CONFIRM_HANDSHAKE indicates that CryptoConnect will immediately confirm
    // the handshake and establish encryption.  This behavior will never happen
    // in the field, but is convenient for higher level tests.
    CONFIRM_HANDSHAKE,

    // ZERO_RTT indicates that CryptoConnect will establish encryption but will
    // not confirm the handshake.
    ZERO_RTT,

    // COLD_START indicates that CryptoConnect will neither establish encryption
    // nor confirm the handshake
    COLD_START,

    // USE_DEFAULT_CRYPTO_STREAM indicates that MockCryptoClientStreamFactory
    // will create a QuicCryptoClientStream instead of a
    // MockCryptoClientStream.
    USE_DEFAULT_CRYPTO_STREAM,
  };

  MockCryptoClientStream(
      const QuicServerId& server_id,
      QuicSpdyClientSessionBase* session,
      ProofVerifyContext* verify_context,
      const QuicConfig& config,
      QuicCryptoClientConfig* crypto_config,
      HandshakeMode handshake_mode,
      const ProofVerifyDetailsChromium* proof_verify_details_,
      bool use_mock_crypter);
  ~MockCryptoClientStream() override;

  // CryptoFramerVisitorInterface implementation.
  void OnHandshakeMessage(const CryptoHandshakeMessage& message) override;

  // QuicCryptoClientStream implementation.
  bool CryptoConnect() override;
  bool encryption_established() const override;
  bool handshake_confirmed() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;

  // Invokes the sessions's CryptoHandshakeEvent method with the specified
  // event.
  void SendOnCryptoHandshakeEvent(QuicSession::CryptoHandshakeEvent event);

  HandshakeMode handshake_mode_;

 protected:
  using QuicCryptoClientStream::session;

 private:
  void SetConfigNegotiated();

  bool encryption_established_;
  bool handshake_confirmed_;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters>
      crypto_negotiated_params_;
  CryptoFramer crypto_framer_;
  bool use_mock_crypter_;

  const QuicServerId server_id_;
  const ProofVerifyDetailsChromium* proof_verify_details_;
  const QuicConfig config_;

  DISALLOW_COPY_AND_ASSIGN(MockCryptoClientStream);
};

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_MOCK_CRYPTO_CLIENT_STREAM_H_
