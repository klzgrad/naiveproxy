// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_simple_server_session.h"

#include <utility>

#include "absl/memory/memory.h"
#include "quiche/quic/core/http/quic_server_initiated_spdy_stream.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/tools/quic_simple_server_stream.h"

namespace quic {

QuicSimpleServerSession::QuicSimpleServerSession(
    const QuicConfig& config, const ParsedQuicVersionVector& supported_versions,
    QuicConnection* connection, QuicSession::Visitor* visitor,
    QuicCryptoServerStreamBase::Helper* helper,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    QuicSimpleServerBackend* quic_simple_server_backend)
    : QuicServerSessionBase(config, supported_versions, connection, visitor,
                            helper, crypto_config, compressed_certs_cache),
      highest_promised_stream_id_(
          QuicUtils::GetInvalidStreamId(connection->transport_version())),
      quic_simple_server_backend_(quic_simple_server_backend) {
  QUICHE_DCHECK(quic_simple_server_backend_);
}

QuicSimpleServerSession::~QuicSimpleServerSession() { DeleteConnection(); }

std::unique_ptr<QuicCryptoServerStreamBase>
QuicSimpleServerSession::CreateQuicCryptoServerStream(
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache) {
  return CreateCryptoServerStream(crypto_config, compressed_certs_cache, this,
                                  stream_helper());
}

void QuicSimpleServerSession::OnStreamFrame(const QuicStreamFrame& frame) {
  if (!IsIncomingStream(frame.stream_id) && !WillNegotiateWebTransport()) {
    QUIC_LOG(WARNING) << "Client shouldn't send data on server push stream";
    connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Client sent data on server push stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  QuicSpdySession::OnStreamFrame(frame);
}

QuicSpdyStream* QuicSimpleServerSession::CreateIncomingStream(QuicStreamId id) {
  if (!ShouldCreateIncomingStream(id)) {
    return nullptr;
  }

  QuicSpdyStream* stream = new QuicSimpleServerStream(
      id, this, BIDIRECTIONAL, quic_simple_server_backend_);
  ActivateStream(absl::WrapUnique(stream));
  return stream;
}

QuicSpdyStream* QuicSimpleServerSession::CreateIncomingStream(
    PendingStream* pending) {
  QuicSpdyStream* stream =
      new QuicSimpleServerStream(pending, this, quic_simple_server_backend_);
  ActivateStream(absl::WrapUnique(stream));
  return stream;
}

QuicSpdyStream* QuicSimpleServerSession::CreateOutgoingBidirectionalStream() {
  if (!WillNegotiateWebTransport()) {
    QUIC_BUG(QuicSimpleServerSession CreateOutgoingBidirectionalStream without
                 WebTransport support)
        << "QuicSimpleServerSession::CreateOutgoingBidirectionalStream called "
           "in a session without WebTransport support.";
    return nullptr;
  }
  if (!ShouldCreateOutgoingBidirectionalStream()) {
    return nullptr;
  }

  QuicServerInitiatedSpdyStream* stream = new QuicServerInitiatedSpdyStream(
      GetNextOutgoingBidirectionalStreamId(), this, BIDIRECTIONAL);
  ActivateStream(absl::WrapUnique(stream));
  return stream;
}

QuicSimpleServerStream*
QuicSimpleServerSession::CreateOutgoingUnidirectionalStream() {
  if (!ShouldCreateOutgoingUnidirectionalStream()) {
    return nullptr;
  }

  QuicSimpleServerStream* stream = new QuicSimpleServerStream(
      GetNextOutgoingUnidirectionalStreamId(), this, WRITE_UNIDIRECTIONAL,
      quic_simple_server_backend_);
  ActivateStream(absl::WrapUnique(stream));
  return stream;
}

void QuicSimpleServerSession::HandleFrameOnNonexistentOutgoingStream(
    QuicStreamId stream_id) {
  // If this stream is a promised but not created stream (stream_id within the
  // range of next_outgoing_stream_id_ and highes_promised_stream_id_),
  // connection shouldn't be closed.
  // Otherwise behave in the same way as base class.
  if (highest_promised_stream_id_ ==
          QuicUtils::GetInvalidStreamId(transport_version()) ||
      stream_id > highest_promised_stream_id_) {
    QuicSession::HandleFrameOnNonexistentOutgoingStream(stream_id);
  }
}

void QuicSimpleServerSession::HandleRstOnValidNonexistentStream(
    const QuicRstStreamFrame& frame) {
  QuicSession::HandleRstOnValidNonexistentStream(frame);
  if (!IsClosedStream(frame.stream_id)) {
    // If a nonexistent stream is not a closed stream and still valid, it must
    // be a locally preserved stream. Resetting this kind of stream means
    // cancelling the promised server push.
    // Since PromisedStreamInfo are queued in sequence, the corresponding
    // index for it in promised_streams_ can be calculated.
    QuicStreamId next_stream_id = next_outgoing_unidirectional_stream_id();
    if ((!version().HasIetfQuicFrames() ||
         !QuicUtils::IsBidirectionalStreamId(frame.stream_id, version())) &&
        frame.stream_id >= next_stream_id) {
      size_t index = (frame.stream_id - next_stream_id) /
                     QuicUtils::StreamIdDelta(transport_version());
      if (index < promised_streams_.size()) {
        promised_streams_[index].is_cancelled = true;
      }
    }
    control_frame_manager().WriteOrBufferRstStream(
        frame.stream_id,
        QuicResetStreamError::FromInternal(QUIC_RST_ACKNOWLEDGEMENT), 0);
    connection()->OnStreamReset(frame.stream_id, QUIC_RST_ACKNOWLEDGEMENT);
  }
}

spdy::Http2HeaderBlock QuicSimpleServerSession::SynthesizePushRequestHeaders(
    std::string request_url, QuicBackendResponse::ServerPushInfo resource,
    const spdy::Http2HeaderBlock& original_request_headers) {
  QuicUrl push_request_url = resource.request_url;

  spdy::Http2HeaderBlock spdy_headers = original_request_headers.Clone();
  // :authority could be different from original request.
  spdy_headers[":authority"] = push_request_url.host();
  spdy_headers[":path"] = push_request_url.path();
  // Push request always use GET.
  spdy_headers[":method"] = "GET";
  spdy_headers["referer"] = request_url;
  spdy_headers[":scheme"] = push_request_url.scheme();
  // It is not possible to push a response to a request that includes a request
  // body.
  spdy_headers["content-length"] = "0";
  // Remove "host" field as push request is a directly generated HTTP2 request
  // which should use ":authority" instead of "host".
  spdy_headers.erase("host");
  return spdy_headers;
}

void QuicSimpleServerSession::SendPushPromise(QuicStreamId original_stream_id,
                                              QuicStreamId promised_stream_id,
                                              spdy::Http2HeaderBlock headers) {
  QUIC_DLOG(INFO) << "stream " << original_stream_id
                  << " send PUSH_PROMISE for promised stream "
                  << promised_stream_id;
  WritePushPromise(original_stream_id, promised_stream_id, std::move(headers));
}

void QuicSimpleServerSession::HandlePromisedPushRequests() {
  while (!promised_streams_.empty() &&
         ShouldCreateOutgoingUnidirectionalStream()) {
    PromisedStreamInfo& promised_info = promised_streams_.front();
    QUICHE_DCHECK_EQ(next_outgoing_unidirectional_stream_id(),
                     promised_info.stream_id);

    if (promised_info.is_cancelled) {
      // This stream has been reset by client. Skip this stream id.
      promised_streams_.pop_front();
      GetNextOutgoingUnidirectionalStreamId();
      return;
    }

    QuicSimpleServerStream* promised_stream =
        static_cast<QuicSimpleServerStream*>(
            CreateOutgoingUnidirectionalStream());
    QUICHE_DCHECK_NE(promised_stream, nullptr);
    QUICHE_DCHECK_EQ(promised_info.stream_id, promised_stream->id());
    QUIC_DLOG(INFO) << "created server push stream " << promised_stream->id();

    promised_stream->SetPriority(
        QuicStreamPriority{promised_info.precedence.spdy3_priority(),
                           QuicStreamPriority::kDefaultIncremental});

    spdy::Http2HeaderBlock request_headers(
        std::move(promised_info.request_headers));

    promised_streams_.pop_front();
    promised_stream->PushResponse(std::move(request_headers));
  }
}

void QuicSimpleServerSession::OnCanCreateNewOutgoingStream(
    bool unidirectional) {
  QuicSpdySession::OnCanCreateNewOutgoingStream(unidirectional);
  if (unidirectional) {
    HandlePromisedPushRequests();
  }
}

void QuicSimpleServerSession::MaybeInitializeHttp3UnidirectionalStreams() {
  size_t previous_static_stream_count = num_static_streams();
  QuicSpdySession::MaybeInitializeHttp3UnidirectionalStreams();
  size_t current_static_stream_count = num_static_streams();
  QUICHE_DCHECK_GE(current_static_stream_count, previous_static_stream_count);
  highest_promised_stream_id_ +=
      QuicUtils::StreamIdDelta(transport_version()) *
      (current_static_stream_count - previous_static_stream_count);
}
}  // namespace quic
