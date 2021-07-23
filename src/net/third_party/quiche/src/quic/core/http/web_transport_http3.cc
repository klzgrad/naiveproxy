// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/http/web_transport_http3.h"

#include <memory>

#include "absl/strings/string_view.h"
#include "quic/core/http/quic_spdy_session.h"
#include "quic/core/http/quic_spdy_stream.h"
#include "quic/core/quic_data_reader.h"
#include "quic/core/quic_data_writer.h"
#include "quic/core/quic_stream.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_utils.h"
#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_bug_tracker.h"
#include "common/platform/api/quiche_logging.h"

#define ENDPOINT \
  (session_->perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

namespace quic {

namespace {
class QUIC_NO_EXPORT NoopWebTransportVisitor : public WebTransportVisitor {
  void OnSessionReady() override {}
  void OnIncomingBidirectionalStreamAvailable() override {}
  void OnIncomingUnidirectionalStreamAvailable() override {}
  void OnDatagramReceived(absl::string_view /*datagram*/) override {}
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {}
};
}  // namespace

WebTransportHttp3::WebTransportHttp3(QuicSpdySession* session,
                                     QuicSpdyStream* connect_stream,
                                     WebTransportSessionId id,
                                     QuicDatagramFlowId flow_id)
    : session_(session),
      connect_stream_(connect_stream),
      id_(id),
      flow_id_(flow_id),
      visitor_(std::make_unique<NoopWebTransportVisitor>()) {
  QUICHE_DCHECK(session_->SupportsWebTransport());
  QUICHE_DCHECK(IsValidWebTransportSessionId(id, session_->version()));
  QUICHE_DCHECK_EQ(connect_stream_->id(), id);
  session_->RegisterHttp3FlowId(flow_id, this);
}

void WebTransportHttp3::AssociateStream(QuicStreamId stream_id) {
  streams_.insert(stream_id);

  ParsedQuicVersion version = session_->version();
  if (QuicUtils::IsOutgoingStreamId(version, stream_id,
                                    session_->perspective())) {
    return;
  }
  if (QuicUtils::IsBidirectionalStreamId(stream_id, version)) {
    incoming_bidirectional_streams_.push_back(stream_id);
    visitor_->OnIncomingBidirectionalStreamAvailable();
  } else {
    incoming_unidirectional_streams_.push_back(stream_id);
    visitor_->OnIncomingUnidirectionalStreamAvailable();
  }
}

void WebTransportHttp3::CloseAllAssociatedStreams() {
  // Copy the stream list before iterating over it, as calls to ResetStream()
  // can potentially mutate the |session_| list.
  std::vector<QuicStreamId> streams(streams_.begin(), streams_.end());
  streams_.clear();
  for (QuicStreamId id : streams) {
    session_->ResetStream(id, QUIC_STREAM_WEBTRANSPORT_SESSION_GONE);
  }
  session_->UnregisterHttp3FlowId(flow_id_);
}

void WebTransportHttp3::HeadersReceived(const spdy::SpdyHeaderBlock& headers) {
  if (session_->perspective() == Perspective::IS_CLIENT) {
    auto it = headers.find(":status");
    if (it == headers.end() || it->second != "200") {
      QUIC_DVLOG(1) << ENDPOINT
                    << "Received WebTransport headers from server without "
                       "status 200, rejecting.";
      return;
    }
  }

  QUIC_DVLOG(1) << ENDPOINT << "WebTransport session " << id_ << " ready.";
  ready_ = true;
  visitor_->OnSessionReady();
  session_->ProcessBufferedWebTransportStreamsForSession(this);
}

WebTransportStream* WebTransportHttp3::AcceptIncomingBidirectionalStream() {
  while (!incoming_bidirectional_streams_.empty()) {
    QuicStreamId id = incoming_bidirectional_streams_.front();
    incoming_bidirectional_streams_.pop_front();
    QuicSpdyStream* stream = session_->GetOrCreateSpdyDataStream(id);
    if (stream == nullptr) {
      // Skip the streams that were reset in between the time they were
      // receieved and the time the client has polled for them.
      continue;
    }
    return stream->web_transport_stream();
  }
  return nullptr;
}

WebTransportStream* WebTransportHttp3::AcceptIncomingUnidirectionalStream() {
  while (!incoming_unidirectional_streams_.empty()) {
    QuicStreamId id = incoming_unidirectional_streams_.front();
    incoming_unidirectional_streams_.pop_front();
    QuicStream* stream = session_->GetOrCreateStream(id);
    if (stream == nullptr) {
      // Skip the streams that were reset in between the time they were
      // receieved and the time the client has polled for them.
      continue;
    }
    return static_cast<WebTransportHttp3UnidirectionalStream*>(stream)
        ->interface();
  }
  return nullptr;
}

bool WebTransportHttp3::CanOpenNextOutgoingBidirectionalStream() {
  return session_->CanOpenOutgoingBidirectionalWebTransportStream(id_);
}
bool WebTransportHttp3::CanOpenNextOutgoingUnidirectionalStream() {
  return session_->CanOpenOutgoingUnidirectionalWebTransportStream(id_);
}
WebTransportStream* WebTransportHttp3::OpenOutgoingBidirectionalStream() {
  QuicSpdyStream* stream =
      session_->CreateOutgoingBidirectionalWebTransportStream(this);
  if (stream == nullptr) {
    // If stream cannot be created due to flow control or other errors, return
    // nullptr.
    return nullptr;
  }
  return stream->web_transport_stream();
}

WebTransportStream* WebTransportHttp3::OpenOutgoingUnidirectionalStream() {
  WebTransportHttp3UnidirectionalStream* stream =
      session_->CreateOutgoingUnidirectionalWebTransportStream(this);
  if (stream == nullptr) {
    // If stream cannot be created due to flow control, return nullptr.
    return nullptr;
  }
  return stream->interface();
}

MessageStatus WebTransportHttp3::SendOrQueueDatagram(QuicMemSlice datagram) {
  return session_->SendHttp3Datagram(
      flow_id_, absl::string_view(datagram.data(), datagram.length()));
}

void WebTransportHttp3::SetDatagramMaxTimeInQueue(
    QuicTime::Delta max_time_in_queue) {
  session_->SetMaxTimeInQueueForFlowId(flow_id_, max_time_in_queue);
}

void WebTransportHttp3::OnHttp3Datagram(QuicDatagramFlowId flow_id,
                                        absl::string_view payload) {
  QUICHE_DCHECK_EQ(flow_id, flow_id_);
  visitor_->OnDatagramReceived(payload);
}

WebTransportHttp3UnidirectionalStream::WebTransportHttp3UnidirectionalStream(
    PendingStream* pending,
    QuicSpdySession* session)
    : QuicStream(pending, session, READ_UNIDIRECTIONAL, /*is_static=*/false),
      session_(session),
      adapter_(session, this, sequencer()),
      needs_to_send_preamble_(false) {}

WebTransportHttp3UnidirectionalStream::WebTransportHttp3UnidirectionalStream(
    QuicStreamId id,
    QuicSpdySession* session,
    WebTransportSessionId session_id)
    : QuicStream(id, session, /*is_static=*/false, WRITE_UNIDIRECTIONAL),
      session_(session),
      adapter_(session, this, sequencer()),
      session_id_(session_id),
      needs_to_send_preamble_(true) {}

void WebTransportHttp3UnidirectionalStream::WritePreamble() {
  if (!needs_to_send_preamble_ || !session_id_.has_value()) {
    QUIC_BUG(WebTransportHttp3UnidirectionalStream duplicate preamble)
        << ENDPOINT << "Sending preamble on stream ID " << id()
        << " at the wrong time.";
    OnUnrecoverableError(QUIC_INTERNAL_ERROR,
                         "Attempting to send a WebTransport unidirectional "
                         "stream preamble at the wrong time.");
    return;
  }

  QuicConnection::ScopedPacketFlusher flusher(session_->connection());
  char buffer[sizeof(uint64_t) * 2];  // varint62, varint62
  QuicDataWriter writer(sizeof(buffer), buffer);
  bool success = true;
  success = success && writer.WriteVarInt62(kWebTransportUnidirectionalStream);
  success = success && writer.WriteVarInt62(*session_id_);
  QUICHE_DCHECK(success);
  WriteOrBufferData(absl::string_view(buffer, writer.length()), /*fin=*/false,
                    /*ack_listener=*/nullptr);
  QUIC_DVLOG(1) << ENDPOINT << "Sent stream type and session ID ("
                << *session_id_ << ") on WebTransport stream " << id();
  needs_to_send_preamble_ = false;
}

bool WebTransportHttp3UnidirectionalStream::ReadSessionId() {
  iovec iov;
  if (!sequencer()->GetReadableRegion(&iov)) {
    return false;
  }
  QuicDataReader reader(static_cast<const char*>(iov.iov_base), iov.iov_len);
  WebTransportSessionId session_id;
  uint8_t session_id_length = reader.PeekVarInt62Length();
  if (!reader.ReadVarInt62(&session_id)) {
    // If all of the data has been received, and we still cannot associate the
    // stream with a session, consume all of the data so that the stream can
    // be closed.
    if (sequencer()->NumBytesConsumed() + sequencer()->NumBytesBuffered() >=
        sequencer()->close_offset()) {
      QUIC_DLOG(WARNING)
          << ENDPOINT << "Failed to associate WebTransport stream " << id()
          << " with a session because the stream ended prematurely.";
      sequencer()->MarkConsumed(sequencer()->NumBytesBuffered());
    }
    return false;
  }
  sequencer()->MarkConsumed(session_id_length);
  session_id_ = session_id;
  session_->AssociateIncomingWebTransportStreamWithSession(session_id, id());
  return true;
}

void WebTransportHttp3UnidirectionalStream::OnDataAvailable() {
  if (!session_id_.has_value()) {
    if (!ReadSessionId()) {
      return;
    }
  }

  adapter_.OnDataAvailable();
}

void WebTransportHttp3UnidirectionalStream::OnCanWriteNewData() {
  adapter_.OnCanWriteNewData();
}

void WebTransportHttp3UnidirectionalStream::OnClose() {
  QuicStream::OnClose();

  if (!session_id_.has_value()) {
    return;
  }
  WebTransportHttp3* session = session_->GetWebTransportSession(*session_id_);
  if (session == nullptr) {
    QUIC_DLOG(WARNING) << ENDPOINT << "WebTransport stream " << id()
                       << " attempted to notify parent session " << *session_id_
                       << ", but the session could not be found.";
    return;
  }
  session->OnStreamClosed(id());
}

}  // namespace quic
