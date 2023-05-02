// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CRYPTO_CLIENT_STREAM_H_
#define QUICHE_QUIC_CORE_QUIC_CRYPTO_CLIENT_STREAM_H_

#include <cstdint>
#include <memory>
#include <string>

#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/proto/cached_network_parameters_proto.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_crypto_handshaker.h"
#include "quiche/quic/core/quic_crypto_stream.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class QuicCryptoClientStreamPeer;
}  // namespace test

class TlsClientHandshaker;

class QUIC_EXPORT_PRIVATE QuicCryptoClientStreamBase : public QuicCryptoStream {
 public:
  explicit QuicCryptoClientStreamBase(QuicSession* session);

  ~QuicCryptoClientStreamBase() override {}

  // Performs a crypto handshake with the server. Returns true if the connection
  // is still connected.
  virtual bool CryptoConnect() = 0;

  // DEPRECATED: Use IsResumption, EarlyDataAccepted, and/or
  // ReceivedInchoateReject instead.
  //
  // num_sent_client_hellos returns the number of client hello messages that
  // have been sent. If the handshake has completed then this is one greater
  // than the number of round-trips needed for the handshake.
  virtual int num_sent_client_hellos() const = 0;

  // Returns true if the handshake performed was a resumption instead of a full
  // handshake. Resumption only makes sense for TLS handshakes - there is no
  // concept of resumption for QUIC crypto even though it supports a 0-RTT
  // handshake. This function only returns valid results once the handshake is
  // complete.
  virtual bool IsResumption() const = 0;

  // Returns true if early data (0-RTT) was accepted in the connection.
  virtual bool EarlyDataAccepted() const = 0;

  // Returns true if the client received an inchoate REJ during the handshake,
  // extending the handshake by one round trip. This only applies for QUIC
  // crypto handshakes. The equivalent feature in IETF QUIC is a Retry packet,
  // but that is handled at the connection layer instead of the crypto layer.
  virtual bool ReceivedInchoateReject() const = 0;

  // The number of server config update messages received by the
  // client.  Does not count update messages that were received prior
  // to handshake confirmation.
  virtual int num_scup_messages_received() const = 0;

  bool ExportKeyingMaterial(absl::string_view /*label*/,
                            absl::string_view /*context*/,
                            size_t /*result_len*/,
                            std::string* /*result*/) override {
    QUICHE_NOTREACHED();
    return false;
  }

  std::string GetAddressToken(
      const CachedNetworkParameters* /*cached_network_params*/) const override {
    QUICHE_DCHECK(false);
    return "";
  }

  bool ValidateAddressToken(absl::string_view /*token*/) const override {
    QUICHE_DCHECK(false);
    return false;
  }

  const CachedNetworkParameters* PreviousCachedNetworkParams() const override {
    QUICHE_DCHECK(false);
    return nullptr;
  }

  void SetPreviousCachedNetworkParams(
      CachedNetworkParameters /*cached_network_params*/) override {
    QUICHE_DCHECK(false);
  }
};

class QUIC_EXPORT_PRIVATE QuicCryptoClientStream
    : public QuicCryptoClientStreamBase {
 public:
  // kMaxClientHellos is the maximum number of times that we'll send a client
  // hello. The value 4 accounts for:
  //   * One failure due to an incorrect or missing source-address token.
  //   * One failure due the server's certificate chain being unavailible and
  //     the server being unwilling to send it without a valid source-address
  //     token.
  //   * One failure due to the ServerConfig private key being located on a
  //     remote oracle which has become unavailable, forcing the server to send
  //     the client a fallback ServerConfig.
  static const int kMaxClientHellos = 4;

  // QuicCryptoClientStream creates a HandshakerInterface at construction time
  // based on the QuicTransportVersion of the connection. Different
  // HandshakerInterfaces provide implementations of different crypto handshake
  // protocols. Currently QUIC crypto is the only protocol implemented; a future
  // HandshakerInterface will use TLS as the handshake protocol.
  // QuicCryptoClientStream delegates all of its public methods to its
  // HandshakerInterface.
  //
  // This setup of the crypto stream delegating its implementation to the
  // handshaker results in the handshaker reading and writing bytes on the
  // crypto stream, instead of the handshaker passing the stream bytes to send.
  class QUIC_EXPORT_PRIVATE HandshakerInterface {
   public:
    virtual ~HandshakerInterface() {}

    // Performs a crypto handshake with the server. Returns true if the
    // connection is still connected.
    virtual bool CryptoConnect() = 0;

    // DEPRECATED: Use IsResumption, EarlyDataAccepted, and/or
    // ReceivedInchoateReject instead.
    //
    // num_sent_client_hellos returns the number of client hello messages that
    // have been sent. If the handshake has completed then this is one greater
    // than the number of round-trips needed for the handshake.
    virtual int num_sent_client_hellos() const = 0;

    // Returns true if the handshake performed was a resumption instead of a
    // full handshake. Resumption only makes sense for TLS handshakes - there is
    // no concept of resumption for QUIC crypto even though it supports a 0-RTT
    // handshake. This function only returns valid results once the handshake is
    // complete.
    virtual bool IsResumption() const = 0;

    // Returns true if early data (0-RTT) was accepted in the connection.
    virtual bool EarlyDataAccepted() const = 0;

    // Returns the ssl_early_data_reason_t describing why 0-RTT was accepted or
    // rejected.
    virtual ssl_early_data_reason_t EarlyDataReason() const = 0;

    // Returns true if the client received an inchoate REJ during the handshake,
    // extending the handshake by one round trip. This only applies for QUIC
    // crypto handshakes. The equivalent feature in IETF QUIC is a Retry packet,
    // but that is handled at the connection layer instead of the crypto layer.
    virtual bool ReceivedInchoateReject() const = 0;

    // The number of server config update messages received by the
    // client.  Does not count update messages that were received prior
    // to handshake confirmation.
    virtual int num_scup_messages_received() const = 0;

    virtual std::string chlo_hash() const = 0;

    // Returns true once any encrypter (initial/0RTT or final/1RTT) has been set
    // for the connection.
    virtual bool encryption_established() const = 0;

    // Returns true if receiving CRYPTO_FRAME at encryption `level` is expected.
    virtual bool IsCryptoFrameExpectedForEncryptionLevel(
        EncryptionLevel level) const = 0;

    // Returns the encryption level to send CRYPTO_FRAME for `space`.
    virtual EncryptionLevel GetEncryptionLevelToSendCryptoDataOfSpace(
        PacketNumberSpace space) const = 0;

    // Returns true once 1RTT keys are available.
    virtual bool one_rtt_keys_available() const = 0;

    // Returns the parameters negotiated in the crypto handshake.
    virtual const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
        const = 0;

    // Used by QuicCryptoStream to parse data received on this stream.
    virtual CryptoMessageParser* crypto_message_parser() = 0;

    // Used by QuicCryptoStream to know how much unprocessed data can be
    // buffered at each encryption level.
    virtual size_t BufferSizeLimitForLevel(EncryptionLevel level) const = 0;

    // Called to generate a decrypter for the next key phase. Each call should
    // generate the key for phase n+1.
    virtual std::unique_ptr<QuicDecrypter>
    AdvanceKeysAndCreateCurrentOneRttDecrypter() = 0;

    // Called to generate an encrypter for the same key phase of the last
    // decrypter returned by AdvanceKeysAndCreateCurrentOneRttDecrypter().
    virtual std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() = 0;

    // Returns current handshake state.
    virtual HandshakeState GetHandshakeState() const = 0;

    // Called when a 1RTT packet has been acknowledged.
    virtual void OnOneRttPacketAcknowledged() = 0;

    // Called when a packet of ENCRYPTION_HANDSHAKE gets sent.
    virtual void OnHandshakePacketSent() = 0;

    // Called when connection gets closed.
    virtual void OnConnectionClosed(QuicErrorCode error,
                                    ConnectionCloseSource source) = 0;

    // Called when handshake done has been received.
    virtual void OnHandshakeDoneReceived() = 0;

    // Called when new token has been received.
    virtual void OnNewTokenReceived(absl::string_view token) = 0;

    // Called when application state is received.
    virtual void SetServerApplicationStateForResumption(
        std::unique_ptr<ApplicationState> application_state) = 0;

    // Called to obtain keying material export of length |result_len| with the
    // given |label| and |context|. Returns false on failure.
    virtual bool ExportKeyingMaterial(absl::string_view label,
                                      absl::string_view context,
                                      size_t result_len,
                                      std::string* result) = 0;
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

  QuicCryptoClientStream(const QuicServerId& server_id, QuicSession* session,
                         std::unique_ptr<ProofVerifyContext> verify_context,
                         QuicCryptoClientConfig* crypto_config,
                         ProofHandler* proof_handler,
                         bool has_application_state);
  QuicCryptoClientStream(const QuicCryptoClientStream&) = delete;
  QuicCryptoClientStream& operator=(const QuicCryptoClientStream&) = delete;

  ~QuicCryptoClientStream() override;

  // From QuicCryptoClientStreamBase
  bool CryptoConnect() override;
  int num_sent_client_hellos() const override;
  bool IsResumption() const override;
  bool EarlyDataAccepted() const override;
  ssl_early_data_reason_t EarlyDataReason() const override;
  bool ReceivedInchoateReject() const override;

  int num_scup_messages_received() const override;

  // From QuicCryptoStream
  bool encryption_established() const override;
  bool one_rtt_keys_available() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;
  void OnPacketDecrypted(EncryptionLevel /*level*/) override {}
  void OnOneRttPacketAcknowledged() override;
  void OnHandshakePacketSent() override;
  void OnConnectionClosed(QuicErrorCode error,
                          ConnectionCloseSource source) override;
  void OnHandshakeDoneReceived() override;
  void OnNewTokenReceived(absl::string_view token) override;
  HandshakeState GetHandshakeState() const override;
  void SetServerApplicationStateForResumption(
      std::unique_ptr<ApplicationState> application_state) override;
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const override;
  std::unique_ptr<QuicDecrypter> AdvanceKeysAndCreateCurrentOneRttDecrypter()
      override;
  std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() override;
  SSL* GetSsl() const override;
  bool IsCryptoFrameExpectedForEncryptionLevel(
      EncryptionLevel level) const override;
  EncryptionLevel GetEncryptionLevelToSendCryptoDataOfSpace(
      PacketNumberSpace space) const override;

  bool ExportKeyingMaterial(absl::string_view label, absl::string_view context,
                            size_t result_len, std::string* result) override;
  std::string chlo_hash() const;

 protected:
  void set_handshaker(std::unique_ptr<HandshakerInterface> handshaker) {
    handshaker_ = std::move(handshaker);
  }

 private:
  friend class test::QuicCryptoClientStreamPeer;
  std::unique_ptr<HandshakerInterface> handshaker_;
  // Points to |handshaker_| if it uses TLS1.3. Otherwise, nullptr.
  // TODO(danzh) change the type of |handshaker_| to TlsClientHandshaker after
  // deprecating Google QUIC.
  TlsClientHandshaker* tls_handshaker_{nullptr};
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CRYPTO_CLIENT_STREAM_H_
