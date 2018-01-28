// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_CRYPTO_CLIENT_STREAM_H_
#define NET_QUIC_CORE_QUIC_CRYPTO_CLIENT_STREAM_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/macros.h"
#include "net/quic/core/crypto/channel_id.h"
#include "net/quic/core/crypto/proof_verifier.h"
#include "net/quic/core/crypto/quic_crypto_client_config.h"
#include "net/quic/core/quic_config.h"
#include "net/quic/core/quic_crypto_handshaker.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/core/quic_server_id.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

class QUIC_EXPORT_PRIVATE QuicCryptoClientStreamBase : public QuicCryptoStream {
 public:
  explicit QuicCryptoClientStreamBase(QuicSession* session);

  ~QuicCryptoClientStreamBase() override {}

  // Performs a crypto handshake with the server. Returns true if the connection
  // is still connected.
  virtual bool CryptoConnect() = 0;

  // num_sent_client_hellos returns the number of client hello messages that
  // have been sent. If the handshake has completed then this is one greater
  // than the number of round-trips needed for the handshake.
  virtual int num_sent_client_hellos() const = 0;

  // The number of server config update messages received by the
  // client.  Does not count update messages that were received prior
  // to handshake confirmation.
  virtual int num_scup_messages_received() const = 0;
};

class QUIC_EXPORT_PRIVATE QuicCryptoClientStream
    : public QuicCryptoClientStreamBase {
 public:
  // kMaxClientHellos is the maximum number of times that we'll send a client
  // hello. The value 3 accounts for:
  //   * One failure due to an incorrect or missing source-address token.
  //   * One failure due the server's certificate chain being unavailible and
  //     the server being unwilling to send it without a valid source-address
  //     token.
  static const int kMaxClientHellos = 3;

  // QuicCryptoClientStream creates a HandshakerDelegate at construction time
  // based on the QuicTransportVersion of the connection. Different
  // HandshakerDelegates provide implementations of different crypto handshake
  // protocols. Currently QUIC crypto is the only protocol implemented; a future
  // HandshakerDelegate will use TLS as the handshake protocol.
  // QuicCryptoClientStream delegates all of its public methods to its
  // HandshakerDelegate.
  //
  // This setup of the crypto stream delegating its implementation to the
  // handshaker results in the handshaker reading and writing bytes on the
  // crypto stream, instead of the handshaker passing the stream bytes to send.
  class QUIC_EXPORT_PRIVATE HandshakerDelegate {
   public:
    virtual ~HandshakerDelegate() {}

    // Performs a crypto handshake with the server. Returns true if the
    // connection is still connected.
    virtual bool CryptoConnect() = 0;

    // num_sent_client_hellos returns the number of client hello messages that
    // have been sent. If the handshake has completed then this is one greater
    // than the number of round-trips needed for the handshake.
    virtual int num_sent_client_hellos() const = 0;

    // The number of server config update messages received by the
    // client.  Does not count update messages that were received prior
    // to handshake confirmation.
    virtual int num_scup_messages_received() const = 0;

    // Returns true if a channel ID was sent on this connection.
    virtual bool WasChannelIDSent() const = 0;

    // Returns true if our ChannelIDSourceCallback was run, which implies the
    // ChannelIDSource operated asynchronously. Intended for testing.
    virtual bool WasChannelIDSourceCallbackRun() const = 0;

    virtual std::string chlo_hash() const = 0;

    // Returns true once any encrypter (initial/0RTT or final/1RTT) has been set
    // for the connection.
    virtual bool encryption_established() const = 0;

    // Returns true once the crypto handshake has completed.
    virtual bool handshake_confirmed() const = 0;

    // Returns the parameters negotiated in the crypto handshake.
    virtual const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
        const = 0;

    // Used by QuicCryptoStream to parse data received on this stream.
    virtual CryptoMessageParser* crypto_message_parser() = 0;
  };

  // ProofHandler is an interface that handles callbacks from the crypto
  // stream when the client has proof verification details of the server.
  class QUIC_EXPORT_PRIVATE ProofHandler {
   public:
    virtual ~ProofHandler() {}

    // Called when the proof in |cached| is marked valid.  If this is a secure
    // QUIC session, then this will happen only after the proof verifier
    // completes.
    virtual void OnProofValid(
        const QuicCryptoClientConfig::CachedState& cached) = 0;

    // Called when proof verification details become available, either because
    // proof verification is complete, or when cached details are used. This
    // will only be called for secure QUIC connections.
    virtual void OnProofVerifyDetailsAvailable(
        const ProofVerifyDetails& verify_details) = 0;
  };

  QuicCryptoClientStream(const QuicServerId& server_id,
                         QuicSession* session,
                         ProofVerifyContext* verify_context,
                         QuicCryptoClientConfig* crypto_config,
                         ProofHandler* proof_handler);

  ~QuicCryptoClientStream() override;

  // From QuicCryptoClientStreamBase
  bool CryptoConnect() override;
  int num_sent_client_hellos() const override;

  int num_scup_messages_received() const override;

  // From QuicCryptoStream
  bool encryption_established() const override;
  bool handshake_confirmed() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;

  // Returns true if a channel ID was sent on this connection.
  bool WasChannelIDSent() const;

  // Returns true if our ChannelIDSourceCallback was run, which implies the
  // ChannelIDSource operated asynchronously. Intended for testing.
  bool WasChannelIDSourceCallbackRun() const;

  std::string chlo_hash() const;

 private:
  std::unique_ptr<HandshakerDelegate> handshaker_;

  DISALLOW_COPY_AND_ASSIGN(QuicCryptoClientStream);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_CRYPTO_CLIENT_STREAM_H_
