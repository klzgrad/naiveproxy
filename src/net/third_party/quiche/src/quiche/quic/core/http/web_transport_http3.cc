// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/web_transport_http3.h"

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/capsule.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport.h"

#define ENDPOINT \
  (session_->perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

namespace quic {

namespace {
class NoopWebTransportVisitor : public WebTransportVisitor {
  void OnSessionReady() override {}
  void OnSessionClosed(WebTransportSessionError /*error_code*/,
                       const std::string& /*error_message*/) override {}
  void OnIncomingBidirectionalStreamAvailable() override {}
  void OnIncomingUnidirectionalStreamAvailable() override {}
  void OnDatagramReceived(absl::string_view /*datagram*/) override {}
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {}
};
}  // namespace

WebTransportHttp3::WebTransportHttp3(QuicSpdySession* session,
                                     QuicSpdyStream* connect_stream,
                                     WebTransportSessionId id)
    : session_(session),
      connect_stream_(connect_stream),
      id_(id),
      visitor_(std::make_unique<NoopWebTransportVisitor>()) {
  QUICHE_DCHECK(session_->SupportsWebTransport());
  QUICHE_DCHECK(IsValidWebTransportSessionId(id, session_->version()));
  QUICHE_DCHECK_EQ(connect_stream_->id(), id);
  connect_stream_->RegisterHttp3DatagramVisitor(this);
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

void WebTransportHttp3::OnConnectStreamClosing() {
  // Copy the stream list before iterating over it, as calls to ResetStream()
  // can potentially mutate the |session_| list.
  std::vector<QuicStreamId> streams(streams_.begin(), streams_.end());
  streams_.clear();
  for (QuicStreamId id : streams) {
    session_->ResetStream(id, QUIC_STREAM_WEBTRANSPORT_SESSION_GONE);
  }
  connect_stream_->UnregisterHttp3DatagramVisitor();

  MaybeNotifyClose();
}

void WebTransportHttp3::CloseSession(WebTransportSessionError error_code,
                                     absl::string_view error_message) {
  if (close_sent_) {
    QUIC_BUG(WebTransportHttp3 close sent twice)
        << "Calling WebTransportHttp3::CloseSession() more than once is not "
           "allowed.";
    return;
  }
  close_sent_ = true;

  // There can be a race between us trying to send our close and peer sending
  // one.  If we received a close, however, we cannot send ours since we already
  // closed the stream in response.
  if (close_received_) {
    QUIC_DLOG(INFO) << "Not sending CLOSE_WEBTRANSPORT_SESSION as we've "
                       "already sent one from peer.";
    return;
  }

  error_code_ = error_code;
  error_message_ = std::string(error_message);
  QuicConnection::ScopedPacketFlusher flusher(
      connect_stream_->spdy_session()->connection());
  connect_stream_->WriteCapsule(
      quiche::Capsule::CloseWebTransportSession(error_code, error_message),
      /*fin=*/true);
}

void WebTransportHttp3::OnCloseReceived(WebTransportSessionError error_code,
                                        absl::string_view error_message) {
  if (close_received_) {
    QUIC_BUG(WebTransportHttp3 notified of close received twice)
        << "WebTransportHttp3::OnCloseReceived() may be only called once.";
  }
  close_received_ = true;

  // If the peer has sent a close after we sent our own, keep the local error.
  if (close_sent_) {
    QUIC_DLOG(INFO) << "Ignoring received CLOSE_WEBTRANSPORT_SESSION as we've "
                       "already sent our own.";
    return;
  }

  error_code_ = error_code;
  error_message_ = std::string(error_message);
  connect_stream_->WriteOrBufferBody("", /*fin=*/true);
  MaybeNotifyClose();
}

void WebTransportHttp3::OnConnectStreamFinReceived() {
  // If we already received a CLOSE_WEBTRANSPORT_SESSION capsule, we don't need
  // to do anything about receiving a FIN, since we already sent one in
  // response.
  if (close_received_) {
    return;
  }
  close_received_ = true;
  if (close_sent_) {
    QUIC_DLOG(INFO) << "Ignoring received FIN as we've already sent our close.";
    return;
  }

  connect_stream_->WriteOrBufferBody("", /*fin=*/true);
  MaybeNotifyClose();
}

void WebTransportHttp3::CloseSessionWithFinOnlyForTests() {
  QUICHE_DCHECK(!close_sent_);
  close_sent_ = true;
  if (close_received_) {
    return;
  }

  connect_stream_->WriteOrBufferBody("", /*fin=*/true);
}

void WebTransportHttp3::HeadersReceived(const spdy::Http2HeaderBlock& headers) {
  if (session_->perspective() == Perspective::IS_CLIENT) {
    int status_code;
    if (!QuicSpdyStream::ParseHeaderStatusCode(headers, &status_code)) {
      QUIC_DVLOG(1) << ENDPOINT
                    << "Received WebTransport headers from server without "
                       "a valid status code, rejecting.";
      rejection_reason_ = WebTransportHttp3RejectionReason::kNoStatusCode;
      return;
    }
    bool valid_status = status_code >= 200 && status_code <= 299;
    if (!valid_status) {
      QUIC_DVLOG(1) << ENDPOINT
                    << "Received WebTransport headers from server with "
                       "status code "
                    << status_code << ", rejecting.";
      rejection_reason_ = WebTransportHttp3RejectionReason::kWrongStatusCode;
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

webtransport::Stream* WebTransportHttp3::GetStreamById(
    webtransport::StreamId id) {
  if (!streams_.contains(id)) {
    return nullptr;
  }
  QuicStream* stream = session_->GetActiveStream(id);
  const bool bidi = QuicUtils::IsBidirectionalStreamId(
      id, ParsedQuicVersion::RFCv1());  // Assume IETF QUIC for WebTransport
  if (bidi) {
    return static_cast<QuicSpdyStream*>(stream)->web_transport_stream();
  } else {
    return static_cast<WebTransportHttp3UnidirectionalStream*>(stream)
        ->interface();
  }
}

webtransport::DatagramStatus WebTransportHttp3::SendOrQueueDatagram(
    absl::string_view datagram) {
  return MessageStatusToWebTransportStatus(
      connect_stream_->SendHttp3Datagram(datagram));
}

QuicByteCount WebTransportHttp3::GetMaxDatagramSize() const {
  return connect_stream_->GetMaxDatagramSize();
}

void WebTransportHttp3::SetDatagramMaxTimeInQueue(
    absl::Duration max_time_in_queue) {
  connect_stream_->SetMaxDatagramTimeInQueue(QuicTimeDelta(max_time_in_queue));
}

void WebTransportHttp3::NotifySessionDraining() {
  if (!drain_sent_) {
    connect_stream_->WriteCapsule(
        quiche::Capsule(quiche::DrainWebTransportSessionCapsule()));
    drain_sent_ = true;
  }
}

void WebTransportHttp3::OnHttp3Datagram(QuicStreamId stream_id,
                                        absl::string_view payload) {
  QUICHE_DCHECK_EQ(stream_id, connect_stream_->id());
  visitor_->OnDatagramReceived(payload);
}

void WebTransportHttp3::MaybeNotifyClose() {
  if (close_notified_) {
    return;
  }
  close_notified_ = true;
  visitor_->OnSessionClosed(error_code_, error_message_);
}

void WebTransportHttp3::OnGoAwayReceived() {
  if (drain_callback_ != nullptr) {
    std::move(drain_callback_)();
    drain_callback_ = nullptr;
  }
}

void WebTransportHttp3::OnDrainSessionReceived() { OnGoAwayReceived(); }

WebTransportHttp3UnidirectionalStream::WebTransportHttp3UnidirectionalStream(
    PendingStream* pending, QuicSpdySession* session)
    : QuicStream(pending, session, /*is_static=*/false),
      session_(session),
      adapter_(session, this, sequencer(), std::nullopt),
      needs_to_send_preamble_(false) {
  sequencer()->set_level_triggered(true);
}

WebTransportHttp3UnidirectionalStream::WebTransportHttp3UnidirectionalStream(
    QuicStreamId id, QuicSpdySession* session, WebTransportSessionId session_id)
    : QuicStream(id, session, /*is_static=*/false, WRITE_UNIDIRECTIONAL),
      session_(session),
      adapter_(session, this, sequencer(), session_id),
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
    if (sequencer()->IsAllDataAvailable()) {
      QUIC_DLOG(WARNING)
          << ENDPOINT << "Failed to associate WebTransport stream " << id()
          << " with a session because the stream ended prematurely.";
      sequencer()->MarkConsumed(sequencer()->NumBytesBuffered());
    }
    return false;
  }
  sequencer()->MarkConsumed(session_id_length);
  session_id_ = session_id;
  adapter_.SetSessionId(session_id);
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

void WebTransportHttp3UnidirectionalStream::OnStreamReset(
    const QuicRstStreamFrame& frame) {
  if (adapter_.visitor() != nullptr) {
    adapter_.visitor()->OnResetStreamReceived(
        Http3ErrorToWebTransportOrDefault(frame.ietf_error_code));
  }
  QuicStream::OnStreamReset(frame);
}
bool WebTransportHttp3UnidirectionalStream::OnStopSending(
    QuicResetStreamError error) {
  if (adapter_.visitor() != nullptr) {
    adapter_.visitor()->OnStopSendingReceived(
        Http3ErrorToWebTransportOrDefault(error.ietf_application_code()));
  }
  return QuicStream::OnStopSending(error);
}
void WebTransportHttp3UnidirectionalStream::OnWriteSideInDataRecvdState() {
  if (adapter_.visitor() != nullptr) {
    adapter_.visitor()->OnWriteSideInDataRecvdState();
  }

  QuicStream::OnWriteSideInDataRecvdState();
}

namespace {
constexpr uint64_t kWebTransportMappedErrorCodeFirst = 0x52e4a40fa8db;
constexpr uint64_t kWebTransportMappedErrorCodeLast = 0x52e5ac983162;
constexpr WebTransportStreamError kDefaultWebTransportError = 0;
}  // namespace

std::optional<WebTransportStreamError> Http3ErrorToWebTransport(
    uint64_t http3_error_code) {
  // Ensure the code is within the valid range.
  if (http3_error_code < kWebTransportMappedErrorCodeFirst ||
      http3_error_code > kWebTransportMappedErrorCodeLast) {
    return std::nullopt;
  }
  // Exclude GREASE codepoints.
  if ((http3_error_code - 0x21) % 0x1f == 0) {
    return std::nullopt;
  }

  uint64_t shifted = http3_error_code - kWebTransportMappedErrorCodeFirst;
  uint64_t result = shifted - shifted / 0x1f;
  QUICHE_DCHECK_LE(result,
                   std::numeric_limits<webtransport::StreamErrorCode>::max());
  return static_cast<WebTransportStreamError>(result);
}

WebTransportStreamError Http3ErrorToWebTransportOrDefault(
    uint64_t http3_error_code) {
  std::optional<WebTransportStreamError> result =
      Http3ErrorToWebTransport(http3_error_code);
  return result.has_value() ? *result : kDefaultWebTransportError;
}

uint64_t WebTransportErrorToHttp3(
    WebTransportStreamError webtransport_error_code) {
  return kWebTransportMappedErrorCodeFirst + webtransport_error_code +
         webtransport_error_code / 0x1e;
}

}  // namespace quic
