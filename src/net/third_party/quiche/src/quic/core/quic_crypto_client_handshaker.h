// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CRYPTO_CLIENT_HANDSHAKER_H_
#define QUICHE_QUIC_CORE_QUIC_CRYPTO_CLIENT_HANDSHAKER_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/proof_verifier.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_client_config.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_client_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_server_id.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// An implementation of QuicCryptoClientStream::HandshakerInterface which uses
// QUIC crypto as the crypto handshake protocol.
class QUIC_EXPORT_PRIVATE QuicCryptoClientHandshaker
    : public QuicCryptoClientStream::HandshakerInterface,
      public QuicCryptoHandshaker {
 public:
  QuicCryptoClientHandshaker(
      const QuicServerId& server_id,
      QuicCryptoClientStream* stream,
      QuicSession* session,
      std::unique_ptr<ProofVerifyContext> verify_context,
      QuicCryptoClientConfig* crypto_config,
      QuicCryptoClientStream::ProofHandler* proof_handler);
  QuicCryptoClientHandshaker(const QuicCryptoClientHandshaker&) = delete;
  QuicCryptoClientHandshaker& operator=(const QuicCryptoClientHandshaker&) =
      delete;

  ~QuicCryptoClientHandshaker() override;

  // From QuicCryptoClientStream::HandshakerInterface
  bool CryptoConnect() override;
  int num_sent_client_hellos() const override;
  bool IsResumption() const override;
  bool EarlyDataAccepted() const override;
  bool ReceivedInchoateReject() const override;
  int num_scup_messages_received() const override;
  std::string chlo_hash() const override;
  bool encryption_established() const override;
  bool one_rtt_keys_available() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;
  HandshakeState GetHandshakeState() const override;
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const override;
  void OnOneRttPacketAcknowledged() override {}
  void OnHandshakeDoneReceived() override;

  // From QuicCryptoHandshaker
  void OnHandshakeMessage(const CryptoHandshakeMessage& message) override;

 protected:
  // Returns the QuicSession that this stream belongs to.
  QuicSession* session() const { return session_; }

  // Send either InchoateClientHello or ClientHello message to the server.
  void DoSendCHLO(QuicCryptoClientConfig::CachedState* cached);

 private:
  // ProofVerifierCallbackImpl is passed as the callback method to VerifyProof.
  // The ProofVerifier calls this class with the result of proof verification
  // when verification is performed asynchronously.
  class QUIC_EXPORT_PRIVATE ProofVerifierCallbackImpl
      : public ProofVerifierCallback {
   public:
    explicit ProofVerifierCallbackImpl(QuicCryptoClientHandshaker* parent);
    ~ProofVerifierCallbackImpl() override;

    // ProofVerifierCallback interface.
    void Run(bool ok,
             const std::string& error_details,
             std::unique_ptr<ProofVerifyDetails>* details) override;

    // Cancel causes any future callbacks to be ignored. It must be called on
    // the same thread as the callback will be made on.
    void Cancel();

   private:
    QuicCryptoClientHandshaker* parent_;
  };

  enum State {
    STATE_IDLE,
    STATE_INITIALIZE,
    STATE_SEND_CHLO,
    STATE_RECV_REJ,
    STATE_VERIFY_PROOF,
    STATE_VERIFY_PROOF_COMPLETE,
    STATE_RECV_SHLO,
    STATE_INITIALIZE_SCUP,
    STATE_NONE,
  };

  // Handles new server config and optional source-address token provided by the
  // server during a connection.
  void HandleServerConfigUpdateMessage(
      const CryptoHandshakeMessage& server_config_update);

  // DoHandshakeLoop performs a step of the handshake state machine. Note that
  // |in| may be nullptr if the call did not result from a received message.
  void DoHandshakeLoop(const CryptoHandshakeMessage* in);

  // Start the handshake process.
  void DoInitialize(QuicCryptoClientConfig::CachedState* cached);

  // Process REJ message from the server.
  void DoReceiveREJ(const CryptoHandshakeMessage* in,
                    QuicCryptoClientConfig::CachedState* cached);

  // Start the proof verification process. Returns the QuicAsyncStatus returned
  // by the ProofVerifier's VerifyProof.
  QuicAsyncStatus DoVerifyProof(QuicCryptoClientConfig::CachedState* cached);

  // If proof is valid then it sets the proof as valid (which persists the
  // server config). If not, it closes the connection.
  void DoVerifyProofComplete(QuicCryptoClientConfig::CachedState* cached);

  // Process SHLO message from the server.
  void DoReceiveSHLO(const CryptoHandshakeMessage* in,
                     QuicCryptoClientConfig::CachedState* cached);

  // Start the proof verification if |server_id_| is https and |cached| has
  // signature.
  void DoInitializeServerConfigUpdate(
      QuicCryptoClientConfig::CachedState* cached);

  // Called to set the proof of |cached| valid.  Also invokes the session's
  // OnProofValid() method.
  void SetCachedProofValid(QuicCryptoClientConfig::CachedState* cached);

  QuicCryptoClientStream* stream_;

  QuicSession* session_;
  HandshakerDelegateInterface* delegate_;

  State next_state_;
  // num_client_hellos_ contains the number of client hello messages that this
  // connection has sent.
  int num_client_hellos_;

  QuicCryptoClientConfig* const crypto_config_;

  // SHA-256 hash of the most recently sent CHLO.
  std::string chlo_hash_;

  // Server's (hostname, port, is_https, privacy_mode) tuple.
  const QuicServerId server_id_;

  // Generation counter from QuicCryptoClientConfig's CachedState.
  uint64_t generation_counter_;

  // verify_context_ contains the context object that we pass to asynchronous
  // proof verifications.
  std::unique_ptr<ProofVerifyContext> verify_context_;

  // proof_verify_callback_ contains the callback object that we passed to an
  // asynchronous proof verification. The ProofVerifier owns this object.
  ProofVerifierCallbackImpl* proof_verify_callback_;
  // proof_handler_ contains the callback object used by a quic client
  // for proof verification. It is not owned by this class.
  QuicCryptoClientStream::ProofHandler* proof_handler_;

  // These members are used to store the result of an asynchronous proof
  // verification. These members must not be used after
  // STATE_VERIFY_PROOF_COMPLETE.
  bool verify_ok_;
  std::string verify_error_details_;
  std::unique_ptr<ProofVerifyDetails> verify_details_;

  QuicTime proof_verify_start_time_;

  int num_scup_messages_received_;

  bool encryption_established_;
  bool one_rtt_keys_available_;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters>
      crypto_negotiated_params_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CRYPTO_CLIENT_HANDSHAKER_H_
