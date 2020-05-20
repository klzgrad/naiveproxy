// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_session.h"

#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_stream.h"
#include "net/third_party/quiche/src/quic/core/http/spdy_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_server_id.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"

namespace quic {

QuicSpdyClientSession::QuicSpdyClientSession(
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection,
    const QuicServerId& server_id,
    QuicCryptoClientConfig* crypto_config,
    QuicClientPushPromiseIndex* push_promise_index)
    : QuicSpdyClientSessionBase(connection,
                                push_promise_index,
                                config,
                                supported_versions),
      server_id_(server_id),
      crypto_config_(crypto_config),
      respect_goaway_(true) {}

QuicSpdyClientSession::~QuicSpdyClientSession() = default;

void QuicSpdyClientSession::Initialize() {
  crypto_stream_ = CreateQuicCryptoStream();
  QuicSpdyClientSessionBase::Initialize();
}

void QuicSpdyClientSession::OnProofValid(
    const QuicCryptoClientConfig::CachedState& /*cached*/) {}

void QuicSpdyClientSession::OnProofVerifyDetailsAvailable(
    const ProofVerifyDetails& /*verify_details*/) {}

bool QuicSpdyClientSession::ShouldCreateOutgoingBidirectionalStream() {
  if (!crypto_stream_->encryption_established()) {
    QUIC_DLOG(INFO) << "Encryption not active so no outgoing stream created.";
    return false;
  }
  bool goaway_received = VersionUsesHttp3(transport_version())
                             ? http3_goaway_received()
                             : QuicSession::goaway_received();
  if (goaway_received && respect_goaway_) {
    QUIC_DLOG(INFO) << "Failed to create a new outgoing stream. "
                    << "Already received goaway.";
    return false;
  }
  return CanOpenNextOutgoingBidirectionalStream();
}

bool QuicSpdyClientSession::ShouldCreateOutgoingUnidirectionalStream() {
  QUIC_BUG << "Try to create outgoing unidirectional client data streams";
  return false;
}

QuicSpdyClientStream*
QuicSpdyClientSession::CreateOutgoingBidirectionalStream() {
  if (!ShouldCreateOutgoingBidirectionalStream()) {
    return nullptr;
  }
  std::unique_ptr<QuicSpdyClientStream> stream = CreateClientStream();
  QuicSpdyClientStream* stream_ptr = stream.get();
  ActivateStream(std::move(stream));
  return stream_ptr;
}

QuicSpdyClientStream*
QuicSpdyClientSession::CreateOutgoingUnidirectionalStream() {
  QUIC_BUG << "Try to create outgoing unidirectional client data streams";
  return nullptr;
}

std::unique_ptr<QuicSpdyClientStream>
QuicSpdyClientSession::CreateClientStream() {
  return std::make_unique<QuicSpdyClientStream>(
      GetNextOutgoingBidirectionalStreamId(), this, BIDIRECTIONAL);
}

QuicCryptoClientStreamBase* QuicSpdyClientSession::GetMutableCryptoStream() {
  return crypto_stream_.get();
}

const QuicCryptoClientStreamBase* QuicSpdyClientSession::GetCryptoStream()
    const {
  return crypto_stream_.get();
}

void QuicSpdyClientSession::CryptoConnect() {
  DCHECK(flow_controller());
  crypto_stream_->CryptoConnect();
}

int QuicSpdyClientSession::GetNumSentClientHellos() const {
  return crypto_stream_->num_sent_client_hellos();
}

bool QuicSpdyClientSession::EarlyDataAccepted() const {
  return crypto_stream_->EarlyDataAccepted();
}

bool QuicSpdyClientSession::ReceivedInchoateReject() const {
  return crypto_stream_->ReceivedInchoateReject();
}

int QuicSpdyClientSession::GetNumReceivedServerConfigUpdates() const {
  return crypto_stream_->num_scup_messages_received();
}

bool QuicSpdyClientSession::ShouldCreateIncomingStream(QuicStreamId id) {
  if (!connection()->connected()) {
    QUIC_BUG << "ShouldCreateIncomingStream called when disconnected";
    return false;
  }
  bool goaway_received = quic::VersionUsesHttp3(transport_version())
                             ? http3_goaway_received()
                             : QuicSession::goaway_received();
  if (goaway_received && respect_goaway_) {
    QUIC_DLOG(INFO) << "Failed to create a new outgoing stream. "
                    << "Already received goaway.";
    return false;
  }

  if (GetQuicReloadableFlag(quic_create_incoming_stream_bug)) {
    if (QuicUtils::IsClientInitiatedStreamId(transport_version(), id)) {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_create_incoming_stream_bug, 1, 2);
      QUIC_BUG << "ShouldCreateIncomingStream called with client initiated "
                  "stream ID.";
      return false;
    } else {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_create_incoming_stream_bug, 2, 2);
    }
  }

  if (QuicUtils::IsClientInitiatedStreamId(transport_version(), id)) {
    QUIC_LOG(WARNING) << "Received invalid push stream id " << id;
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID,
        "Server created non write unidirectional stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  if (VersionHasIetfQuicFrames(transport_version()) &&
      QuicUtils::IsBidirectionalStreamId(id)) {
    connection()->CloseConnection(
        QUIC_HTTP_SERVER_INITIATED_BIDIRECTIONAL_STREAM,
        "Server created bidirectional stream.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  return true;
}

QuicSpdyStream* QuicSpdyClientSession::CreateIncomingStream(
    PendingStream* pending) {
  QuicSpdyStream* stream =
      new QuicSpdyClientStream(pending, this, READ_UNIDIRECTIONAL);
  ActivateStream(QuicWrapUnique(stream));
  return stream;
}

QuicSpdyStream* QuicSpdyClientSession::CreateIncomingStream(QuicStreamId id) {
  if (!ShouldCreateIncomingStream(id)) {
    return nullptr;
  }
  QuicSpdyStream* stream =
      new QuicSpdyClientStream(id, this, READ_UNIDIRECTIONAL);
  ActivateStream(QuicWrapUnique(stream));
  return stream;
}

std::unique_ptr<QuicCryptoClientStreamBase>
QuicSpdyClientSession::CreateQuicCryptoStream() {
  return std::make_unique<QuicCryptoClientStream>(
      server_id_, this,
      crypto_config_->proof_verifier()->CreateDefaultContext(), crypto_config_,
      this);
}

bool QuicSpdyClientSession::IsAuthorized(const std::string& /*authority*/) {
  return true;
}

}  // namespace quic
