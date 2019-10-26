// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_HANDSHAKER_H_
#define QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_HANDSHAKER_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/proto/cached_network_parameters_proto.h"
#include "net/third_party/quiche/src/quic/core/proto/source_address_token_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_handshaker.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class QuicCryptoServerStreamPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicCryptoServerHandshaker
    : public QuicCryptoServerStream::HandshakerDelegate,
      public QuicCryptoHandshaker {
 public:
  // |crypto_config| must outlive the stream.
  // |session| must outlive the stream.
  // |helper| must outlive the stream.
  QuicCryptoServerHandshaker(const QuicCryptoServerConfig* crypto_config,
                             QuicCryptoServerStream* stream,
                             QuicCompressedCertsCache* compressed_certs_cache,
                             QuicSession* session,
                             QuicCryptoServerStream::Helper* helper);
  QuicCryptoServerHandshaker(const QuicCryptoServerHandshaker&) = delete;
  QuicCryptoServerHandshaker& operator=(const QuicCryptoServerHandshaker&) =
      delete;

  ~QuicCryptoServerHandshaker() override;

  // From HandshakerDelegate
  void CancelOutstandingCallbacks() override;
  bool GetBase64SHA256ClientChannelID(std::string* output) const override;
  void SendServerConfigUpdate(
      const CachedNetworkParameters* cached_network_params) override;
  uint8_t NumHandshakeMessages() const override;
  uint8_t NumHandshakeMessagesWithServerNonces() const override;
  int NumServerConfigUpdateMessagesSent() const override;
  const CachedNetworkParameters* PreviousCachedNetworkParams() const override;
  bool ZeroRttAttempted() const override;
  void SetPreviousCachedNetworkParams(
      CachedNetworkParameters cached_network_params) override;
  bool ShouldSendExpectCTHeader() const override;

  // From QuicCryptoStream
  bool encryption_established() const override;
  bool handshake_confirmed() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const override;

  // From QuicCryptoHandshaker
  void OnHandshakeMessage(const CryptoHandshakeMessage& message) override;

 protected:
  virtual void ProcessClientHello(
      QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
          result,
      std::unique_ptr<ProofSource::Details> proof_source_details,
      std::unique_ptr<ProcessClientHelloResultCallback> done_cb);

  // Hook that allows the server to set QuicConfig defaults just
  // before going through the parameter negotiation step.
  virtual void OverrideQuicConfigDefaults(QuicConfig* config);

  // Returns client address used to generate and validate source address token.
  virtual const QuicSocketAddress GetClientAddress();

  // Returns the QuicSession that this stream belongs to.
  QuicSession* session() const { return session_; }

  void set_encryption_established(bool encryption_established) {
    encryption_established_ = encryption_established;
  }

  void set_handshake_confirmed(bool handshake_confirmed) {
    handshake_confirmed_ = handshake_confirmed;
  }

 private:
  friend class test::QuicCryptoServerStreamPeer;

  class ValidateCallback : public ValidateClientHelloResultCallback {
   public:
    explicit ValidateCallback(QuicCryptoServerHandshaker* parent);
    ValidateCallback(const ValidateCallback&) = delete;
    ValidateCallback& operator=(const ValidateCallback&) = delete;
    // To allow the parent to detach itself from the callback before deletion.
    void Cancel();

    // From ValidateClientHelloResultCallback
    void Run(QuicReferenceCountedPointer<Result> result,
             std::unique_ptr<ProofSource::Details> details) override;

   private:
    QuicCryptoServerHandshaker* parent_;
  };

  class SendServerConfigUpdateCallback
      : public BuildServerConfigUpdateMessageResultCallback {
   public:
    explicit SendServerConfigUpdateCallback(QuicCryptoServerHandshaker* parent);
    SendServerConfigUpdateCallback(const SendServerConfigUpdateCallback&) =
        delete;
    void operator=(const SendServerConfigUpdateCallback&) = delete;

    // To allow the parent to detach itself from the callback before deletion.
    void Cancel();

    // From BuildServerConfigUpdateMessageResultCallback
    void Run(bool ok, const CryptoHandshakeMessage& message) override;

   private:
    QuicCryptoServerHandshaker* parent_;
  };

  // Invoked by ValidateCallback::RunImpl once initial validation of
  // the client hello is complete.  Finishes processing of the client
  // hello message and handles handshake success/failure.
  void FinishProcessingHandshakeMessage(
      QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
          result,
      std::unique_ptr<ProofSource::Details> details);

  class ProcessClientHelloCallback;
  friend class ProcessClientHelloCallback;

  // Portion of FinishProcessingHandshakeMessage which executes after
  // ProcessClientHello has been called.
  void FinishProcessingHandshakeMessageAfterProcessClientHello(
      const ValidateClientHelloResultCallback::Result& result,
      QuicErrorCode error,
      const std::string& error_details,
      std::unique_ptr<CryptoHandshakeMessage> reply,
      std::unique_ptr<DiversificationNonce> diversification_nonce,
      std::unique_ptr<ProofSource::Details> proof_source_details);

  // Invoked by SendServerConfigUpdateCallback::RunImpl once the proof has been
  // received.  |ok| indicates whether or not the proof was successfully
  // acquired, and |message| holds the partially-constructed message from
  // SendServerConfigUpdate.
  void FinishSendServerConfigUpdate(bool ok,
                                    const CryptoHandshakeMessage& message);

  // Returns the QuicTransportVersion of the connection.
  QuicTransportVersion transport_version() const {
    return session_->connection()->transport_version();
  }

  QuicCryptoServerStream* stream_;

  QuicSession* session_;

  // crypto_config_ contains crypto parameters for the handshake.
  const QuicCryptoServerConfig* crypto_config_;

  // compressed_certs_cache_ contains a set of most recently compressed certs.
  // Owned by QuicDispatcher.
  QuicCompressedCertsCache* compressed_certs_cache_;

  // Server's certificate chain and signature of the server config, as provided
  // by ProofSource::GetProof.
  QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config_;

  // Hash of the last received CHLO message which can be used for generating
  // server config update messages.
  std::string chlo_hash_;

  // Pointer to the helper for this crypto stream. Must outlive this stream.
  QuicCryptoServerStream::Helper* helper_;

  // Number of handshake messages received by this stream.
  uint8_t num_handshake_messages_;

  // Number of handshake messages received by this stream that contain
  // server nonces (indicating that this is a non-zero-RTT handshake
  // attempt).
  uint8_t num_handshake_messages_with_server_nonces_;

  // Pointer to the active callback that will receive the result of
  // BuildServerConfigUpdateMessage and forward it to
  // FinishSendServerConfigUpdate.  nullptr if no update message is currently
  // being built.
  SendServerConfigUpdateCallback* send_server_config_update_cb_;

  // Number of server config update (SCUP) messages sent by this stream.
  int num_server_config_update_messages_sent_;

  // If the client provides CachedNetworkParameters in the STK in the CHLO, then
  // store here, and send back in future STKs if we have no better bandwidth
  // estimate to send.
  std::unique_ptr<CachedNetworkParameters> previous_cached_network_params_;

  // Contains any source address tokens which were present in the CHLO.
  SourceAddressTokens previous_source_address_tokens_;

  // True if client attempts 0-rtt handshake (which can succeed or fail).
  bool zero_rtt_attempted_;

  // Size of the packet containing the most recently received CHLO.
  QuicByteCount chlo_packet_size_;

  // Pointer to the active callback that will receive the result of the client
  // hello validation request and forward it to FinishProcessingHandshakeMessage
  // for processing.  nullptr if no handshake message is being validated.  Note
  // that this field is mutually exclusive with process_client_hello_cb_.
  ValidateCallback* validate_client_hello_cb_;

  // Pointer to the active callback which will receive the results of
  // ProcessClientHello and forward it to
  // FinishProcessingHandshakeMessageAfterProcessClientHello.  Note that this
  // field is mutually exclusive with validate_client_hello_cb_.
  ProcessClientHelloCallback* process_client_hello_cb_;

  bool encryption_established_;
  bool handshake_confirmed_;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters>
      crypto_negotiated_params_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_HANDSHAKER_H_
