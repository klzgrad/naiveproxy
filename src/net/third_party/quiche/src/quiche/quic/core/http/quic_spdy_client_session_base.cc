// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_spdy_client_session_base.h"

#include <string>

#include "quiche/quic/core/http/quic_client_promised_info.h"
#include "quiche/quic/core/http/spdy_server_push_utils.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"

using spdy::Http2HeaderBlock;

namespace quic {

QuicSpdyClientSessionBase::QuicSpdyClientSessionBase(
    QuicConnection* connection, QuicSession::Visitor* visitor,
    QuicClientPushPromiseIndex* push_promise_index, const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions)
    : QuicSpdySession(connection, visitor, config, supported_versions),
      push_promise_index_(push_promise_index),
      largest_promised_stream_id_(
          QuicUtils::GetInvalidStreamId(connection->transport_version())) {}

QuicSpdyClientSessionBase::~QuicSpdyClientSessionBase() {
  //  all promised streams for this session
  for (auto& it : promised_by_id_) {
    QUIC_DVLOG(1) << "erase stream " << it.first << " url " << it.second->url();
    push_promise_index_->promised_by_url()->erase(it.second->url());
  }
  DeleteConnection();
}

void QuicSpdyClientSessionBase::OnConfigNegotiated() {
  QuicSpdySession::OnConfigNegotiated();
}

void QuicSpdyClientSessionBase::OnInitialHeadersComplete(
    QuicStreamId stream_id, const Http2HeaderBlock& response_headers) {
  // Note that the strong ordering of the headers stream means that
  // QuicSpdyClientStream::OnPromiseHeadersComplete must have already
  // been called (on the associated stream) if this is a promised
  // stream. However, this stream may not have existed at this time,
  // hence the need to query the session.
  QuicClientPromisedInfo* promised = GetPromisedById(stream_id);
  if (!promised) return;

  promised->OnResponseHeaders(response_headers);
}

void QuicSpdyClientSessionBase::OnPromiseHeaderList(
    QuicStreamId stream_id, QuicStreamId promised_stream_id, size_t frame_len,
    const QuicHeaderList& header_list) {
  if (IsStaticStream(stream_id)) {
    connection()->CloseConnection(
        QUIC_INVALID_HEADERS_STREAM_DATA, "stream is static",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  // In HTTP3, push promises are received on individual streams, so they could
  // be arrive out of order.
  if (!VersionUsesHttp3(transport_version()) &&
      promised_stream_id !=
          QuicUtils::GetInvalidStreamId(transport_version()) &&
      largest_promised_stream_id_ !=
          QuicUtils::GetInvalidStreamId(transport_version()) &&
      promised_stream_id <= largest_promised_stream_id_) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID,
        "Received push stream id lesser or equal to the"
        " last accepted before",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  if (!IsIncomingStream(promised_stream_id)) {
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Received push stream id for outgoing stream.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (VersionUsesHttp3(transport_version())) {
    // Received push stream id is higher than MAX_PUSH_ID
    // because no MAX_PUSH_ID frame is ever sent.
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID,
        "Received push stream id higher than MAX_PUSH_ID.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  largest_promised_stream_id_ = promised_stream_id;

  QuicSpdyStream* stream = GetOrCreateSpdyDataStream(stream_id);
  if (!stream) {
    // It's quite possible to receive headers after a stream has been reset.
    return;
  }
  stream->OnPromiseHeaderList(promised_stream_id, frame_len, header_list);
}

bool QuicSpdyClientSessionBase::HandlePromised(
    QuicStreamId /* associated_id */, QuicStreamId promised_id,
    const Http2HeaderBlock& headers) {
  // TODO(b/136295430): Do not treat |promised_id| as a stream ID when using
  // IETF QUIC.
  // Due to pathalogical packet re-ordering, it is possible that
  // frames for the promised stream have already arrived, and the
  // promised stream could be active or closed.
  if (IsClosedStream(promised_id)) {
    // There was a RST on the data stream already, perhaps
    // QUIC_REFUSED_STREAM?
    QUIC_DVLOG(1) << "Promise ignored for stream " << promised_id
                  << " that is already closed";
    return false;
  }

  if (push_promise_index_->promised_by_url()->size() >= get_max_promises()) {
    QUIC_DVLOG(1) << "Too many promises, rejecting promise for stream "
                  << promised_id;
    ResetPromised(promised_id, QUIC_REFUSED_STREAM);
    return false;
  }

  const std::string url =
      SpdyServerPushUtils::GetPromisedUrlFromHeaders(headers);
  QuicClientPromisedInfo* old_promised = GetPromisedByUrl(url);
  if (old_promised) {
    QUIC_DVLOG(1) << "Promise for stream " << promised_id
                  << " is duplicate URL " << url
                  << " of previous promise for stream " << old_promised->id();
    ResetPromised(promised_id, QUIC_DUPLICATE_PROMISE_URL);
    return false;
  }

  if (GetPromisedById(promised_id)) {
    // OnPromiseHeadersComplete() would have closed the connection if
    // promised id is a duplicate.
    QUIC_BUG(quic_bug_10412_1) << "Duplicate promise for id " << promised_id;
    return false;
  }

  QuicClientPromisedInfo* promised =
      new QuicClientPromisedInfo(this, promised_id, url);
  std::unique_ptr<QuicClientPromisedInfo> promised_owner(promised);
  promised->Init();
  QUIC_DVLOG(1) << "stream " << promised_id << " emplace url " << url;
  (*push_promise_index_->promised_by_url())[url] = promised;
  promised_by_id_[promised_id] = std::move(promised_owner);
  bool result = promised->OnPromiseHeaders(headers);
  if (result) {
    QUICHE_DCHECK(promised_by_id_.find(promised_id) != promised_by_id_.end());
  }
  return result;
}

QuicClientPromisedInfo* QuicSpdyClientSessionBase::GetPromisedByUrl(
    const std::string& url) {
  auto it = push_promise_index_->promised_by_url()->find(url);
  if (it != push_promise_index_->promised_by_url()->end()) {
    return it->second;
  }
  return nullptr;
}

QuicClientPromisedInfo* QuicSpdyClientSessionBase::GetPromisedById(
    const QuicStreamId id) {
  auto it = promised_by_id_.find(id);
  if (it != promised_by_id_.end()) {
    return it->second.get();
  }
  return nullptr;
}

QuicSpdyStream* QuicSpdyClientSessionBase::GetPromisedStream(
    const QuicStreamId id) {
  QuicStream* stream = GetActiveStream(id);
  if (stream != nullptr) {
    return static_cast<QuicSpdyStream*>(stream);
  }
  return nullptr;
}

void QuicSpdyClientSessionBase::DeletePromised(
    QuicClientPromisedInfo* promised) {
  push_promise_index_->promised_by_url()->erase(promised->url());
  // Since promised_by_id_ contains the unique_ptr, this will destroy
  // promised.
  // ToDo: Consider implementing logic to send a new MAX_PUSH_ID frame to allow
  // another stream to be promised.
  promised_by_id_.erase(promised->id());
  if (!VersionUsesHttp3(transport_version())) {
    headers_stream()->MaybeReleaseSequencerBuffer();
  }
}

void QuicSpdyClientSessionBase::OnPushStreamTimedOut(
    QuicStreamId /*stream_id*/) {}

void QuicSpdyClientSessionBase::ResetPromised(
    QuicStreamId id, QuicRstStreamErrorCode error_code) {
  QUICHE_DCHECK(QuicUtils::IsServerInitiatedStreamId(transport_version(), id));
  ResetStream(id, error_code);
  if (!IsOpenStream(id) && !IsClosedStream(id)) {
    MaybeIncreaseLargestPeerStreamId(id);
  }
}

void QuicSpdyClientSessionBase::OnStreamClosed(QuicStreamId stream_id) {
  QuicSpdySession::OnStreamClosed(stream_id);
  if (!VersionUsesHttp3(transport_version())) {
    headers_stream()->MaybeReleaseSequencerBuffer();
  }
}

bool QuicSpdyClientSessionBase::ShouldReleaseHeadersStreamSequencerBuffer() {
  return !HasActiveRequestStreams() && promised_by_id_.empty();
}

bool QuicSpdyClientSessionBase::ShouldKeepConnectionAlive() const {
  return QuicSpdySession::ShouldKeepConnectionAlive() ||
         num_outgoing_draining_streams() > 0;
}

bool QuicSpdyClientSessionBase::OnSettingsFrame(const SettingsFrame& frame) {
  if (!was_zero_rtt_rejected()) {
    if (max_outbound_header_list_size() != std::numeric_limits<size_t>::max() &&
        frame.values.find(SETTINGS_MAX_FIELD_SECTION_SIZE) ==
            frame.values.end()) {
      CloseConnectionWithDetails(
          QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
          "Server accepted 0-RTT but omitted non-default "
          "SETTINGS_MAX_FIELD_SECTION_SIZE");
      return false;
    }

    if (qpack_encoder()->maximum_blocked_streams() != 0 &&
        frame.values.find(SETTINGS_QPACK_BLOCKED_STREAMS) ==
            frame.values.end()) {
      CloseConnectionWithDetails(
          QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
          "Server accepted 0-RTT but omitted non-default "
          "SETTINGS_QPACK_BLOCKED_STREAMS");
      return false;
    }

    if (qpack_encoder()->MaximumDynamicTableCapacity() != 0 &&
        frame.values.find(SETTINGS_QPACK_MAX_TABLE_CAPACITY) ==
            frame.values.end()) {
      CloseConnectionWithDetails(
          QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
          "Server accepted 0-RTT but omitted non-default "
          "SETTINGS_QPACK_MAX_TABLE_CAPACITY");
      return false;
    }
  }

  if (!QuicSpdySession::OnSettingsFrame(frame)) {
    return false;
  }
  std::string settings_frame = HttpEncoder::SerializeSettingsFrame(frame);
  auto serialized_data = std::make_unique<ApplicationState>(
      settings_frame.data(), settings_frame.data() + settings_frame.length());
  GetMutableCryptoStream()->SetServerApplicationStateForResumption(
      std::move(serialized_data));
  return true;
}

}  // namespace quic
