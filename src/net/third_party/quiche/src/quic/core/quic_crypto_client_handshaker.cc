// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_crypto_client_handshaker.h"

#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_client_stats.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {

QuicCryptoClientHandshaker::ProofVerifierCallbackImpl::
    ProofVerifierCallbackImpl(QuicCryptoClientHandshaker* parent)
    : parent_(parent) {}

QuicCryptoClientHandshaker::ProofVerifierCallbackImpl::
    ~ProofVerifierCallbackImpl() {}

void QuicCryptoClientHandshaker::ProofVerifierCallbackImpl::Run(
    bool ok,
    const std::string& error_details,
    std::unique_ptr<ProofVerifyDetails>* details) {
  if (parent_ == nullptr) {
    return;
  }

  parent_->verify_ok_ = ok;
  parent_->verify_error_details_ = error_details;
  parent_->verify_details_ = std::move(*details);
  parent_->proof_verify_callback_ = nullptr;
  parent_->DoHandshakeLoop(nullptr);

  // The ProofVerifier owns this object and will delete it when this method
  // returns.
}

void QuicCryptoClientHandshaker::ProofVerifierCallbackImpl::Cancel() {
  parent_ = nullptr;
}

QuicCryptoClientHandshaker::QuicCryptoClientHandshaker(
    const QuicServerId& server_id,
    QuicCryptoClientStream* stream,
    QuicSession* session,
    std::unique_ptr<ProofVerifyContext> verify_context,
    QuicCryptoClientConfig* crypto_config,
    QuicCryptoClientStream::ProofHandler* proof_handler)
    : QuicCryptoHandshaker(stream, session),
      stream_(stream),
      session_(session),
      delegate_(session),
      next_state_(STATE_IDLE),
      num_client_hellos_(0),
      crypto_config_(crypto_config),
      server_id_(server_id),
      generation_counter_(0),
      verify_context_(std::move(verify_context)),
      proof_verify_callback_(nullptr),
      proof_handler_(proof_handler),
      verify_ok_(false),
      proof_verify_start_time_(QuicTime::Zero()),
      num_scup_messages_received_(0),
      encryption_established_(false),
      one_rtt_keys_available_(false),
      crypto_negotiated_params_(new QuicCryptoNegotiatedParameters) {}

QuicCryptoClientHandshaker::~QuicCryptoClientHandshaker() {
  if (proof_verify_callback_) {
    proof_verify_callback_->Cancel();
  }
}

void QuicCryptoClientHandshaker::OnHandshakeMessage(
    const CryptoHandshakeMessage& message) {
  QuicCryptoHandshaker::OnHandshakeMessage(message);
  if (message.tag() == kSCUP) {
    if (!one_rtt_keys_available()) {
      stream_->OnUnrecoverableError(
          QUIC_CRYPTO_UPDATE_BEFORE_HANDSHAKE_COMPLETE,
          "Early SCUP disallowed");
      return;
    }

    // |message| is an update from the server, so we treat it differently from a
    // handshake message.
    HandleServerConfigUpdateMessage(message);
    num_scup_messages_received_++;
    return;
  }

  // Do not process handshake messages after the handshake is confirmed.
  if (one_rtt_keys_available()) {
    stream_->OnUnrecoverableError(QUIC_CRYPTO_MESSAGE_AFTER_HANDSHAKE_COMPLETE,
                                  "Unexpected handshake message");
    return;
  }

  DoHandshakeLoop(&message);
}

bool QuicCryptoClientHandshaker::CryptoConnect() {
  next_state_ = STATE_INITIALIZE;
  DoHandshakeLoop(nullptr);
  return session()->connection()->connected();
}

int QuicCryptoClientHandshaker::num_sent_client_hellos() const {
  return num_client_hellos_;
}

bool QuicCryptoClientHandshaker::IsResumption() const {
  QUIC_BUG_IF(!one_rtt_keys_available_);
  // While 0-RTT handshakes could be considered to be like resumption, QUIC
  // Crypto doesn't have the same notion of a resumption like TLS does.
  return false;
}

bool QuicCryptoClientHandshaker::EarlyDataAccepted() const {
  QUIC_BUG_IF(!one_rtt_keys_available_);
  return num_client_hellos_ == 1;
}

bool QuicCryptoClientHandshaker::ReceivedInchoateReject() const {
  QUIC_BUG_IF(!one_rtt_keys_available_);
  return num_client_hellos_ >= 3;
}

int QuicCryptoClientHandshaker::num_scup_messages_received() const {
  return num_scup_messages_received_;
}

std::string QuicCryptoClientHandshaker::chlo_hash() const {
  return chlo_hash_;
}

bool QuicCryptoClientHandshaker::encryption_established() const {
  return encryption_established_;
}

bool QuicCryptoClientHandshaker::one_rtt_keys_available() const {
  return one_rtt_keys_available_;
}

const QuicCryptoNegotiatedParameters&
QuicCryptoClientHandshaker::crypto_negotiated_params() const {
  return *crypto_negotiated_params_;
}

CryptoMessageParser* QuicCryptoClientHandshaker::crypto_message_parser() {
  return QuicCryptoHandshaker::crypto_message_parser();
}

HandshakeState QuicCryptoClientHandshaker::GetHandshakeState() const {
  return one_rtt_keys_available() ? HANDSHAKE_COMPLETE : HANDSHAKE_START;
}

void QuicCryptoClientHandshaker::OnHandshakeDoneReceived() {
  DCHECK(false);
}

size_t QuicCryptoClientHandshaker::BufferSizeLimitForLevel(
    EncryptionLevel level) const {
  return QuicCryptoHandshaker::BufferSizeLimitForLevel(level);
}

void QuicCryptoClientHandshaker::HandleServerConfigUpdateMessage(
    const CryptoHandshakeMessage& server_config_update) {
  DCHECK(server_config_update.tag() == kSCUP);
  std::string error_details;
  QuicCryptoClientConfig::CachedState* cached =
      crypto_config_->LookupOrCreate(server_id_);
  QuicErrorCode error = crypto_config_->ProcessServerConfigUpdate(
      server_config_update, session()->connection()->clock()->WallNow(),
      session()->transport_version(), chlo_hash_, cached,
      crypto_negotiated_params_, &error_details);

  if (error != QUIC_NO_ERROR) {
    stream_->OnUnrecoverableError(
        error, "Server config update invalid: " + error_details);
    return;
  }

  DCHECK(one_rtt_keys_available());
  if (proof_verify_callback_) {
    proof_verify_callback_->Cancel();
  }
  next_state_ = STATE_INITIALIZE_SCUP;
  DoHandshakeLoop(nullptr);
}

void QuicCryptoClientHandshaker::DoHandshakeLoop(
    const CryptoHandshakeMessage* in) {
  QuicCryptoClientConfig::CachedState* cached =
      crypto_config_->LookupOrCreate(server_id_);

  QuicAsyncStatus rv = QUIC_SUCCESS;
  do {
    CHECK_NE(STATE_NONE, next_state_);
    const State state = next_state_;
    next_state_ = STATE_IDLE;
    rv = QUIC_SUCCESS;
    switch (state) {
      case STATE_INITIALIZE:
        DoInitialize(cached);
        break;
      case STATE_SEND_CHLO:
        DoSendCHLO(cached);
        return;  // return waiting to hear from server.
      case STATE_RECV_REJ:
        DoReceiveREJ(in, cached);
        break;
      case STATE_VERIFY_PROOF:
        rv = DoVerifyProof(cached);
        break;
      case STATE_VERIFY_PROOF_COMPLETE:
        DoVerifyProofComplete(cached);
        break;
      case STATE_RECV_SHLO:
        DoReceiveSHLO(in, cached);
        break;
      case STATE_IDLE:
        // This means that the peer sent us a message that we weren't expecting.
        stream_->OnUnrecoverableError(QUIC_INVALID_CRYPTO_MESSAGE_TYPE,
                                      "Handshake in idle state");
        return;
      case STATE_INITIALIZE_SCUP:
        DoInitializeServerConfigUpdate(cached);
        break;
      case STATE_NONE:
        QUIC_NOTREACHED();
        return;  // We are done.
    }
  } while (rv != QUIC_PENDING && next_state_ != STATE_NONE);
}

void QuicCryptoClientHandshaker::DoInitialize(
    QuicCryptoClientConfig::CachedState* cached) {
  if (!cached->IsEmpty() && !cached->signature().empty()) {
    // Note that we verify the proof even if the cached proof is valid.
    // This allows us to respond to CA trust changes or certificate
    // expiration because it may have been a while since we last verified
    // the proof.
    DCHECK(crypto_config_->proof_verifier());
    // Track proof verification time when cached server config is used.
    proof_verify_start_time_ = session()->connection()->clock()->Now();
    chlo_hash_ = cached->chlo_hash();
    // If the cached state needs to be verified, do it now.
    next_state_ = STATE_VERIFY_PROOF;
  } else {
    next_state_ = STATE_SEND_CHLO;
  }
}

void QuicCryptoClientHandshaker::DoSendCHLO(
    QuicCryptoClientConfig::CachedState* cached) {
  // Send the client hello in plaintext.
  session()->connection()->SetDefaultEncryptionLevel(ENCRYPTION_INITIAL);
  encryption_established_ = false;
  if (num_client_hellos_ >= QuicCryptoClientStream::kMaxClientHellos) {
    stream_->OnUnrecoverableError(
        QUIC_CRYPTO_TOO_MANY_REJECTS,
        quiche::QuicheStrCat("More than ",
                             QuicCryptoClientStream::kMaxClientHellos,
                             " rejects"));
    return;
  }
  num_client_hellos_++;

  CryptoHandshakeMessage out;
  DCHECK(session() != nullptr);
  DCHECK(session()->config() != nullptr);
  // Send all the options, regardless of whether we're sending an
  // inchoate or subsequent hello.
  session()->config()->ToHandshakeMessage(&out, session()->transport_version());

  if (!cached->IsComplete(session()->connection()->clock()->WallNow())) {
    crypto_config_->FillInchoateClientHello(
        server_id_, session()->supported_versions().front(), cached,
        session()->connection()->random_generator(),
        /* demand_x509_proof= */ true, crypto_negotiated_params_, &out);
    // Pad the inchoate client hello to fill up a packet.
    const QuicByteCount kFramingOverhead = 50;  // A rough estimate.
    const QuicByteCount max_packet_size =
        session()->connection()->max_packet_length();
    if (max_packet_size <= kFramingOverhead) {
      QUIC_DLOG(DFATAL) << "max_packet_length (" << max_packet_size
                        << ") has no room for framing overhead.";
      stream_->OnUnrecoverableError(QUIC_INTERNAL_ERROR,
                                    "max_packet_size too smalll");
      return;
    }
    if (kClientHelloMinimumSize > max_packet_size - kFramingOverhead) {
      QUIC_DLOG(DFATAL) << "Client hello won't fit in a single packet.";
      stream_->OnUnrecoverableError(QUIC_INTERNAL_ERROR, "CHLO too large");
      return;
    }
    next_state_ = STATE_RECV_REJ;
    chlo_hash_ = CryptoUtils::HashHandshakeMessage(out, Perspective::IS_CLIENT);
    session()->connection()->set_fully_pad_crypto_handshake_packets(
        crypto_config_->pad_inchoate_hello());
    SendHandshakeMessage(out);
    return;
  }

  std::string error_details;
  QuicErrorCode error = crypto_config_->FillClientHello(
      server_id_, session()->connection()->connection_id(),
      session()->supported_versions().front(),
      session()->connection()->version(), cached,
      session()->connection()->clock()->WallNow(),
      session()->connection()->random_generator(), crypto_negotiated_params_,
      &out, &error_details);
  if (error != QUIC_NO_ERROR) {
    // Flush the cached config so that, if it's bad, the server has a
    // chance to send us another in the future.
    cached->InvalidateServerConfig();
    stream_->OnUnrecoverableError(error, error_details);
    return;
  }
  chlo_hash_ = CryptoUtils::HashHandshakeMessage(out, Perspective::IS_CLIENT);
  if (cached->proof_verify_details()) {
    proof_handler_->OnProofVerifyDetailsAvailable(
        *cached->proof_verify_details());
  }
  next_state_ = STATE_RECV_SHLO;
  session()->connection()->set_fully_pad_crypto_handshake_packets(
      crypto_config_->pad_full_hello());
  SendHandshakeMessage(out);
  // Be prepared to decrypt with the new server write key.
  delegate_->OnNewEncryptionKeyAvailable(
      ENCRYPTION_ZERO_RTT,
      std::move(crypto_negotiated_params_->initial_crypters.encrypter));
  delegate_->OnNewDecryptionKeyAvailable(
      ENCRYPTION_ZERO_RTT,
      std::move(crypto_negotiated_params_->initial_crypters.decrypter),
      /*set_alternative_decrypter=*/true,
      /*latch_once_used=*/true);
  encryption_established_ = true;
  delegate_->SetDefaultEncryptionLevel(ENCRYPTION_ZERO_RTT);
}

void QuicCryptoClientHandshaker::DoReceiveREJ(
    const CryptoHandshakeMessage* in,
    QuicCryptoClientConfig::CachedState* cached) {
  // We sent a dummy CHLO because we didn't have enough information to
  // perform a handshake, or we sent a full hello that the server
  // rejected. Here we hope to have a REJ that contains the information
  // that we need.
  if (in->tag() != kREJ) {
    next_state_ = STATE_NONE;
    stream_->OnUnrecoverableError(QUIC_INVALID_CRYPTO_MESSAGE_TYPE,
                                  "Expected REJ");
    return;
  }

  QuicTagVector reject_reasons;
  static_assert(sizeof(QuicTag) == sizeof(uint32_t), "header out of sync");
  if (in->GetTaglist(kRREJ, &reject_reasons) == QUIC_NO_ERROR) {
    uint32_t packed_error = 0;
    for (size_t i = 0; i < reject_reasons.size(); ++i) {
      // HANDSHAKE_OK is 0 and don't report that as error.
      if (reject_reasons[i] == HANDSHAKE_OK || reject_reasons[i] >= 32) {
        continue;
      }
      HandshakeFailureReason reason =
          static_cast<HandshakeFailureReason>(reject_reasons[i]);
      packed_error |= 1 << (reason - 1);
    }
    QUIC_DVLOG(1) << "Reasons for rejection: " << packed_error;
    if (num_client_hellos_ == QuicCryptoClientStream::kMaxClientHellos) {
      QuicClientSparseHistogram("QuicClientHelloRejectReasons.TooMany",
                                packed_error);
    }
    QuicClientSparseHistogram("QuicClientHelloRejectReasons.Secure",
                              packed_error);
  }

  // Receipt of a REJ message means that the server received the CHLO
  // so we can cancel and retransmissions.
  delegate_->NeuterUnencryptedData();

  std::string error_details;
  QuicErrorCode error = crypto_config_->ProcessRejection(
      *in, session()->connection()->clock()->WallNow(),
      session()->transport_version(), chlo_hash_, cached,
      crypto_negotiated_params_, &error_details);

  if (error != QUIC_NO_ERROR) {
    next_state_ = STATE_NONE;
    stream_->OnUnrecoverableError(error, error_details);
    return;
  }
  if (!cached->proof_valid()) {
    if (!cached->signature().empty()) {
      // Note that we only verify the proof if the cached proof is not
      // valid. If the cached proof is valid here, someone else must have
      // just added the server config to the cache and verified the proof,
      // so we can assume no CA trust changes or certificate expiration
      // has happened since then.
      next_state_ = STATE_VERIFY_PROOF;
      return;
    }
  }
  next_state_ = STATE_SEND_CHLO;
}

QuicAsyncStatus QuicCryptoClientHandshaker::DoVerifyProof(
    QuicCryptoClientConfig::CachedState* cached) {
  ProofVerifier* verifier = crypto_config_->proof_verifier();
  DCHECK(verifier);
  next_state_ = STATE_VERIFY_PROOF_COMPLETE;
  generation_counter_ = cached->generation_counter();

  ProofVerifierCallbackImpl* proof_verify_callback =
      new ProofVerifierCallbackImpl(this);

  verify_ok_ = false;

  QuicAsyncStatus status = verifier->VerifyProof(
      server_id_.host(), server_id_.port(), cached->server_config(),
      session()->transport_version(), chlo_hash_, cached->certs(),
      cached->cert_sct(), cached->signature(), verify_context_.get(),
      &verify_error_details_, &verify_details_,
      std::unique_ptr<ProofVerifierCallback>(proof_verify_callback));

  switch (status) {
    case QUIC_PENDING:
      proof_verify_callback_ = proof_verify_callback;
      QUIC_DVLOG(1) << "Doing VerifyProof";
      break;
    case QUIC_FAILURE:
      break;
    case QUIC_SUCCESS:
      verify_ok_ = true;
      break;
  }
  return status;
}

void QuicCryptoClientHandshaker::DoVerifyProofComplete(
    QuicCryptoClientConfig::CachedState* cached) {
  if (proof_verify_start_time_.IsInitialized()) {
    QUIC_CLIENT_HISTOGRAM_TIMES(
        "QuicSession.VerifyProofTime.CachedServerConfig",
        (session()->connection()->clock()->Now() - proof_verify_start_time_),
        QuicTime::Delta::FromMilliseconds(1), QuicTime::Delta::FromSeconds(10),
        50, "");
  }
  if (!verify_ok_) {
    if (verify_details_) {
      proof_handler_->OnProofVerifyDetailsAvailable(*verify_details_);
    }
    if (num_client_hellos_ == 0) {
      cached->Clear();
      next_state_ = STATE_INITIALIZE;
      return;
    }
    next_state_ = STATE_NONE;
    QUIC_CLIENT_HISTOGRAM_BOOL("QuicVerifyProofFailed.HandshakeConfirmed",
                               one_rtt_keys_available(), "");
    stream_->OnUnrecoverableError(QUIC_PROOF_INVALID,
                                  "Proof invalid: " + verify_error_details_);
    return;
  }

  // Check if generation_counter has changed between STATE_VERIFY_PROOF and
  // STATE_VERIFY_PROOF_COMPLETE state changes.
  if (generation_counter_ != cached->generation_counter()) {
    next_state_ = STATE_VERIFY_PROOF;
  } else {
    SetCachedProofValid(cached);
    cached->SetProofVerifyDetails(verify_details_.release());
    if (!one_rtt_keys_available()) {
      next_state_ = STATE_SEND_CHLO;
    } else {
      // TODO: Enable Expect-Staple. https://crbug.com/631101
      next_state_ = STATE_NONE;
    }
  }
}

void QuicCryptoClientHandshaker::DoReceiveSHLO(
    const CryptoHandshakeMessage* in,
    QuicCryptoClientConfig::CachedState* cached) {
  next_state_ = STATE_NONE;
  // We sent a CHLO that we expected to be accepted and now we're
  // hoping for a SHLO from the server to confirm that.  First check
  // to see whether the response was a reject, and if so, move on to
  // the reject-processing state.
  if (in->tag() == kREJ) {
    // A reject message must be sent in ENCRYPTION_INITIAL.
    if (session()->connection()->last_decrypted_level() != ENCRYPTION_INITIAL) {
      // The rejection was sent encrypted!
      stream_->OnUnrecoverableError(QUIC_CRYPTO_ENCRYPTION_LEVEL_INCORRECT,
                                    "encrypted REJ message");
      return;
    }
    next_state_ = STATE_RECV_REJ;
    return;
  }

  if (in->tag() != kSHLO) {
    stream_->OnUnrecoverableError(
        QUIC_INVALID_CRYPTO_MESSAGE_TYPE,
        quiche::QuicheStrCat("Expected SHLO or REJ. Received: ",
                             QuicTagToString(in->tag())));
    return;
  }

  if (session()->connection()->last_decrypted_level() == ENCRYPTION_INITIAL) {
    // The server hello was sent without encryption.
    stream_->OnUnrecoverableError(QUIC_CRYPTO_ENCRYPTION_LEVEL_INCORRECT,
                                  "unencrypted SHLO message");
    return;
  }

  std::string error_details;
  QuicErrorCode error = crypto_config_->ProcessServerHello(
      *in, session()->connection()->connection_id(),
      session()->connection()->version(),
      session()->connection()->server_supported_versions(), cached,
      crypto_negotiated_params_, &error_details);

  if (error != QUIC_NO_ERROR) {
    stream_->OnUnrecoverableError(error,
                                  "Server hello invalid: " + error_details);
    return;
  }
  error = session()->config()->ProcessPeerHello(*in, SERVER, &error_details);
  if (error != QUIC_NO_ERROR) {
    stream_->OnUnrecoverableError(error,
                                  "Server hello invalid: " + error_details);
    return;
  }
  session()->OnConfigNegotiated();

  CrypterPair* crypters = &crypto_negotiated_params_->forward_secure_crypters;
  // TODO(agl): we don't currently latch this decrypter because the idea
  // has been floated that the server shouldn't send packets encrypted
  // with the FORWARD_SECURE key until it receives a FORWARD_SECURE
  // packet from the client.
  delegate_->OnNewEncryptionKeyAvailable(ENCRYPTION_FORWARD_SECURE,
                                         std::move(crypters->encrypter));
  delegate_->OnNewDecryptionKeyAvailable(ENCRYPTION_FORWARD_SECURE,
                                         std::move(crypters->decrypter),
                                         /*set_alternative_decrypter=*/true,
                                         /*latch_once_used=*/false);
  one_rtt_keys_available_ = true;
  delegate_->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  delegate_->DiscardOldEncryptionKey(ENCRYPTION_INITIAL);
  delegate_->NeuterHandshakeData();
}

void QuicCryptoClientHandshaker::DoInitializeServerConfigUpdate(
    QuicCryptoClientConfig::CachedState* cached) {
  bool update_ignored = false;
  if (!cached->IsEmpty() && !cached->signature().empty()) {
    // Note that we verify the proof even if the cached proof is valid.
    DCHECK(crypto_config_->proof_verifier());
    next_state_ = STATE_VERIFY_PROOF;
  } else {
    update_ignored = true;
    next_state_ = STATE_NONE;
  }
  QUIC_CLIENT_HISTOGRAM_COUNTS("QuicNumServerConfig.UpdateMessagesIgnored",
                               update_ignored, 1, 1000000, 50, "");
}

void QuicCryptoClientHandshaker::SetCachedProofValid(
    QuicCryptoClientConfig::CachedState* cached) {
  cached->SetProofValid();
  proof_handler_->OnProofValid(*cached);
}

}  // namespace quic
