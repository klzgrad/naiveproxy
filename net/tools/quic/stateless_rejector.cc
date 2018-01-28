// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/stateless_rejector.h"

#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"

namespace net {

class StatelessRejector::ValidateCallback
    : public ValidateClientHelloResultCallback {
 public:
  explicit ValidateCallback(
      std::unique_ptr<StatelessRejector> rejector,
      std::unique_ptr<StatelessRejector::ProcessDoneCallback> cb)
      : rejector_(std::move(rejector)), cb_(std::move(cb)) {}

  ~ValidateCallback() override {}

  void Run(QuicReferenceCountedPointer<Result> result,
           std::unique_ptr<ProofSource::Details> /* proof_source_details */)
      override {
    StatelessRejector* rejector_ptr = rejector_.get();
    rejector_ptr->ProcessClientHello(std::move(result), std::move(rejector_),
                                     std::move(cb_));
  }

 private:
  std::unique_ptr<StatelessRejector> rejector_;
  std::unique_ptr<StatelessRejector::ProcessDoneCallback> cb_;
};

StatelessRejector::StatelessRejector(
    QuicTransportVersion version,
    const QuicTransportVersionVector& versions,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    const QuicClock* clock,
    QuicRandom* random,
    QuicByteCount chlo_packet_size,
    const QuicSocketAddress& client_address,
    const QuicSocketAddress& server_address)
    : state_(UNKNOWN),
      error_(QUIC_INTERNAL_ERROR),
      version_(version),
      versions_(versions),
      connection_id_(0),
      chlo_packet_size_(chlo_packet_size),
      client_address_(client_address),
      server_address_(server_address),
      clock_(clock),
      random_(random),
      crypto_config_(crypto_config),
      compressed_certs_cache_(compressed_certs_cache),
      signed_config_(new QuicSignedServerConfig),
      params_(new QuicCryptoNegotiatedParameters) {}

StatelessRejector::~StatelessRejector() {}

void StatelessRejector::OnChlo(QuicTransportVersion version,
                               QuicConnectionId connection_id,
                               QuicConnectionId server_designated_connection_id,
                               const CryptoHandshakeMessage& message) {
  DCHECK_EQ(kCHLO, message.tag());
  DCHECK_NE(connection_id, server_designated_connection_id);
  DCHECK_EQ(state_, UNKNOWN);

  if (!FLAGS_quic_reloadable_flag_enable_quic_stateless_reject_support ||
      !FLAGS_quic_reloadable_flag_quic_use_cheap_stateless_rejects ||
      !QuicCryptoServerStream::DoesPeerSupportStatelessRejects(message)) {
    state_ = UNSUPPORTED;
    return;
  }

  connection_id_ = connection_id;
  server_designated_connection_id_ = server_designated_connection_id;
  chlo_ = message;  // Note: copies the message
}

void StatelessRejector::Process(std::unique_ptr<StatelessRejector> rejector,
                                std::unique_ptr<ProcessDoneCallback> done_cb) {
  QUIC_BUG_IF(rejector->state() != UNKNOWN) << "StatelessRejector::Process "
                                               "called for a rejector which "
                                               "has already made a decision";
  StatelessRejector* rejector_ptr = rejector.get();
  rejector_ptr->crypto_config_->ValidateClientHello(
      rejector_ptr->chlo_, rejector_ptr->client_address_.host(),
      rejector_ptr->server_address_, rejector_ptr->version_,
      rejector_ptr->clock_, rejector_ptr->signed_config_,
      std::unique_ptr<ValidateCallback>(
          new ValidateCallback(std::move(rejector), std::move(done_cb))));
}

class StatelessRejector::ProcessClientHelloCallback
    : public ProcessClientHelloResultCallback {
 public:
  ProcessClientHelloCallback(
      std::unique_ptr<StatelessRejector> rejector,
      std::unique_ptr<StatelessRejector::ProcessDoneCallback> done_cb)
      : rejector_(std::move(rejector)), done_cb_(std::move(done_cb)) {}

  void Run(QuicErrorCode error,
           const std::string& error_details,
           std::unique_ptr<CryptoHandshakeMessage> message,
           std::unique_ptr<DiversificationNonce> diversification_nonce,
           std::unique_ptr<ProofSource::Details> /* proof_source_details */)
      override {
    StatelessRejector* rejector_ptr = rejector_.get();
    rejector_ptr->ProcessClientHelloDone(
        error, error_details, std::move(message), std::move(rejector_),
        std::move(done_cb_));
  }

 private:
  std::unique_ptr<StatelessRejector> rejector_;
  std::unique_ptr<StatelessRejector::ProcessDoneCallback> done_cb_;
};

void StatelessRejector::ProcessClientHello(
    QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
        result,
    std::unique_ptr<StatelessRejector> rejector,
    std::unique_ptr<StatelessRejector::ProcessDoneCallback> done_cb) {
  std::unique_ptr<ProcessClientHelloCallback> cb(
      new ProcessClientHelloCallback(std::move(rejector), std::move(done_cb)));
  crypto_config_->ProcessClientHello(
      result,
      /*reject_only=*/true, connection_id_, server_address_, client_address_,
      version_, versions_,
      /*use_stateless_rejects=*/true, server_designated_connection_id_, clock_,
      random_, compressed_certs_cache_, params_, signed_config_,
      QuicCryptoStream::CryptoMessageFramingOverhead(version_),
      chlo_packet_size_, std::move(cb));
}

void StatelessRejector::ProcessClientHelloDone(
    QuicErrorCode error,
    const std::string& error_details,
    std::unique_ptr<CryptoHandshakeMessage> message,
    std::unique_ptr<StatelessRejector> rejector,
    std::unique_ptr<StatelessRejector::ProcessDoneCallback> done_cb) {
  reply_ = std::move(message);

  if (error != QUIC_NO_ERROR) {
    error_ = error;
    error_details_ = error_details;
    state_ = FAILED;
  } else if (reply_->tag() == kSREJ) {
    state_ = REJECTED;
  } else {
    state_ = ACCEPTED;
  }
  done_cb->Run(std::move(rejector));
}

}  // namespace net
