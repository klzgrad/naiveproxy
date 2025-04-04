// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_H_
#define QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_H_

#include <string>

#include "quiche/quic/core/proto/cached_network_parameters_proto.h"
#include "quiche/quic/core/proto/source_address_token_proto.h"
#include "quiche/quic/core/quic_crypto_handshaker.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class QuicCryptoServerStreamPeer;
}  // namespace test

class QUICHE_EXPORT QuicCryptoServerStream : public QuicCryptoServerStreamBase,
                                             public QuicCryptoHandshaker {
 public:
  QuicCryptoServerStream(const QuicCryptoServerStream&) = delete;
  QuicCryptoServerStream& operator=(const QuicCryptoServerStream&) = delete;

  ~QuicCryptoServerStream() override;

  // From QuicCryptoServerStreamBase
  void CancelOutstandingCallbacks() override;
  bool GetBase64SHA256ClientChannelID(std::string* output) const override;
  void SendServerConfigUpdate(
      const CachedNetworkParameters* cached_network_params) override;
  bool DisableResumption() override;
  bool IsZeroRtt() const override;
  bool IsResumption() const override;
  bool ResumptionAttempted() const override;
  bool EarlyDataAttempted() const override;
  int NumServerConfigUpdateMessagesSent() const override;
  const CachedNetworkParameters* PreviousCachedNetworkParams() const override;
  void SetPreviousCachedNetworkParams(
      CachedNetworkParameters cached_network_params) override;
  void OnPacketDecrypted(EncryptionLevel level) override;
  void OnOneRttPacketAcknowledged() override {}
  void OnHandshakePacketSent() override {}
  void OnConnectionClosed(const QuicConnectionCloseFrame& /*frame*/,
                          ConnectionCloseSource /*source*/) override {}
  void OnHandshakeDoneReceived() override;
  void OnNewTokenReceived(absl::string_view token) override;
  std::string GetAddressToken(
      const CachedNetworkParameters* /*cached_network_params*/) const override;
  bool ValidateAddressToken(absl::string_view token) const override;
  bool ShouldSendExpectCTHeader() const override;
  bool DidCertMatchSni() const override;
  const ProofSource::Details* ProofSourceDetails() const override;

  // From QuicCryptoStream
  ssl_early_data_reason_t EarlyDataReason() const override;
  bool encryption_established() const override;
  bool one_rtt_keys_available() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;
  HandshakeState GetHandshakeState() const override;
  void SetServerApplicationStateForResumption(
      std::unique_ptr<ApplicationState> state) override;
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const override;
  std::unique_ptr<QuicDecrypter> AdvanceKeysAndCreateCurrentOneRttDecrypter()
      override;
  std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() override;
  SSL* GetSsl() const override;
  bool IsCryptoFrameExpectedForEncryptionLevel(
      EncryptionLevel level) const override;
  EncryptionLevel GetEncryptionLevelToSendCryptoDataOfSpace(
      PacketNumberSpace space) const override;

  // From QuicCryptoHandshaker
  void OnHandshakeMessage(const CryptoHandshakeMessage& message) override;

 protected:
  QUICHE_EXPORT friend std::unique_ptr<QuicCryptoServerStreamBase>
  CreateCryptoServerStream(const QuicCryptoServerConfig* crypto_config,
                           QuicCompressedCertsCache* compressed_certs_cache,
                           QuicSession* session,
                           QuicCryptoServerStreamBase::Helper* helper);

  // |crypto_config| must outlive the stream.
  // |session| must outlive the stream.
  // |helper| must outlive the stream.
  QuicCryptoServerStream(const QuicCryptoServerConfig* crypto_config,
                         QuicCompressedCertsCache* compressed_certs_cache,
                         QuicSession* session,
                         QuicCryptoServerStreamBase::Helper* helper);

  virtual void ProcessClientHello(
      quiche::QuicheReferenceCountedPointer<
          ValidateClientHelloResultCallback::Result>
          result,
      std::unique_ptr<ProofSource::Details> proof_source_details,
      std::shared_ptr<ProcessClientHelloResultCallback> done_cb);

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

  void set_one_rtt_keys_available(bool one_rtt_keys_available) {
    one_rtt_keys_available_ = one_rtt_keys_available;
  }

 private:
  friend class test::QuicCryptoServerStreamPeer;

  class QUICHE_EXPORT ValidateCallback
      : public ValidateClientHelloResultCallback {
   public:
    explicit ValidateCallback(QuicCryptoServerStream* parent);
    ValidateCallback(const ValidateCallback&) = delete;
    ValidateCallback& operator=(const ValidateCallback&) = delete;
    // To allow the parent to detach itself from the callback before deletion.
    void Cancel();

    // From ValidateClientHelloResultCallback
    void Run(quiche::QuicheReferenceCountedPointer<Result> result,
             std::unique_ptr<ProofSource::Details> details) override;

   private:
    QuicCryptoServerStream* parent_;
  };

  class SendServerConfigUpdateCallback
      : public BuildServerConfigUpdateMessageResultCallback {
   public:
    explicit SendServerConfigUpdateCallback(QuicCryptoServerStream* parent);
    SendServerConfigUpdateCallback(const SendServerConfigUpdateCallback&) =
        delete;
    void operator=(const SendServerConfigUpdateCallback&) = delete;

    // To allow the parent to detach itself from the callback before deletion.
    void Cancel();

    // From BuildServerConfigUpdateMessageResultCallback
    void Run(bool ok, const CryptoHandshakeMessage& message) override;

   private:
    QuicCryptoServerStream* parent_;
  };

  // Invoked by ValidateCallback::RunImpl once initial validation of
  // the client hello is complete.  Finishes processing of the client
  // hello message and handles handshake success/failure.
  void FinishProcessingHandshakeMessage(
      quiche::QuicheReferenceCountedPointer<
          ValidateClientHelloResultCallback::Result>
          result,
      std::unique_ptr<ProofSource::Details> details);

  class ProcessClientHelloCallback;
  friend class ProcessClientHelloCallback;

  // Portion of FinishProcessingHandshakeMessage which executes after
  // ProcessClientHello has been called.
  void FinishProcessingHandshakeMessageAfterProcessClientHello(
      const ValidateClientHelloResultCallback::Result& result,
      QuicErrorCode error, const std::string& error_details,
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
    return session_->transport_version();
  }

  QuicSession* session_;
  HandshakerDelegateInterface* delegate_;

  // crypto_config_ contains crypto parameters for the handshake.
  const QuicCryptoServerConfig* crypto_config_;

  // compressed_certs_cache_ contains a set of most recently compressed certs.
  // Owned by QuicDispatcher.
  QuicCompressedCertsCache* compressed_certs_cache_;

  // Server's certificate chain and signature of the server config, as provided
  // by ProofSource::GetProof.
  quiche::QuicheReferenceCountedPointer<QuicSignedServerConfig> signed_config_;

  // Hash of the last received CHLO message which can be used for generating
  // server config update messages.
  std::string chlo_hash_;

  // Pointer to the helper for this crypto stream. Must outlive this stream.
  QuicCryptoServerStreamBase::Helper* helper_;

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
  std::weak_ptr<ProcessClientHelloCallback> process_client_hello_cb_;

  // The ProofSource::Details from this connection.
  std::unique_ptr<ProofSource::Details> proof_source_details_;

  bool encryption_established_;
  bool one_rtt_keys_available_;
  bool one_rtt_packet_decrypted_;
  quiche::QuicheReferenceCountedPointer<QuicCryptoNegotiatedParameters>
      crypto_negotiated_params_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_H_
