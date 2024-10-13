// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_HTTP3_H_
#define QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_HTTP3_H_

#include <memory>
#include <optional>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/time/time.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/http/web_transport_stream_adapter.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/quic/core/web_transport_stats.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

class QuicSpdySession;
class QuicSpdyStream;

enum class WebTransportHttp3RejectionReason {
  kNone,
  kNoStatusCode,
  kWrongStatusCode,
  kMissingDraftVersion,
  kUnsupportedDraftVersion,
};

// A session of WebTransport over HTTP/3.  The session is owned by
// QuicSpdyStream object for the CONNECT stream that established it.
//
// WebTransport over HTTP/3 specification:
// <https://datatracker.ietf.org/doc/html/draft-ietf-webtrans-http3>
class QUICHE_EXPORT WebTransportHttp3
    : public WebTransportSession,
      public QuicSpdyStream::Http3DatagramVisitor {
 public:
  WebTransportHttp3(QuicSpdySession* session, QuicSpdyStream* connect_stream,
                    WebTransportSessionId id);

  void HeadersReceived(const quiche::HttpHeaderBlock& headers);
  void SetVisitor(std::unique_ptr<WebTransportVisitor> visitor) {
    visitor_ = std::move(visitor);
  }

  WebTransportSessionId id() { return id_; }
  bool ready() { return ready_; }

  void AssociateStream(QuicStreamId stream_id);
  void OnStreamClosed(QuicStreamId stream_id) { streams_.erase(stream_id); }
  void OnConnectStreamClosing();

  size_t NumberOfAssociatedStreams() { return streams_.size(); }

  void CloseSession(WebTransportSessionError error_code,
                    absl::string_view error_message) override;
  void OnCloseReceived(WebTransportSessionError error_code,
                       absl::string_view error_message);
  void OnConnectStreamFinReceived();

  // It is legal for WebTransport to be closed without a
  // CLOSE_WEBTRANSPORT_SESSION capsule.  We always send a capsule, but we still
  // need to ensure we handle this case correctly.
  void CloseSessionWithFinOnlyForTests();

  // Return the earliest incoming stream that has been received by the session
  // but has not been accepted.  Returns nullptr if there are no incoming
  // streams.
  WebTransportStream* AcceptIncomingBidirectionalStream() override;
  WebTransportStream* AcceptIncomingUnidirectionalStream() override;

  bool CanOpenNextOutgoingBidirectionalStream() override;
  bool CanOpenNextOutgoingUnidirectionalStream() override;
  WebTransportStream* OpenOutgoingBidirectionalStream() override;
  WebTransportStream* OpenOutgoingUnidirectionalStream() override;

  webtransport::Stream* GetStreamById(webtransport::StreamId id) override;

  webtransport::DatagramStatus SendOrQueueDatagram(
      absl::string_view datagram) override;
  QuicByteCount GetMaxDatagramSize() const override;
  void SetDatagramMaxTimeInQueue(absl::Duration max_time_in_queue) override;

  webtransport::DatagramStats GetDatagramStats() override {
    return WebTransportDatagramStatsForQuicSession(*session_);
  }
  webtransport::SessionStats GetSessionStats() override {
    return WebTransportStatsForQuicSession(*session_);
  }

  void NotifySessionDraining() override;
  void SetOnDraining(quiche::SingleUseCallback<void()> callback) override {
    drain_callback_ = std::move(callback);
  }

  // From QuicSpdyStream::Http3DatagramVisitor.
  void OnHttp3Datagram(QuicStreamId stream_id,
                       absl::string_view payload) override;
  void OnUnknownCapsule(QuicStreamId /*stream_id*/,
                        const quiche::UnknownCapsule& /*capsule*/) override {}

  bool close_received() const { return close_received_; }
  WebTransportHttp3RejectionReason rejection_reason() const {
    return rejection_reason_;
  }

  void OnGoAwayReceived();
  void OnDrainSessionReceived();

 private:
  // Notifies the visitor that the connection has been closed.  Ensures that the
  // visitor is only ever called once.
  void MaybeNotifyClose();

  QuicSpdySession* const session_;        // Unowned.
  QuicSpdyStream* const connect_stream_;  // Unowned.
  const WebTransportSessionId id_;
  // |ready_| is set to true when the peer has seen both sets of headers.
  bool ready_ = false;
  std::unique_ptr<WebTransportVisitor> visitor_;
  absl::flat_hash_set<QuicStreamId> streams_;
  quiche::QuicheCircularDeque<QuicStreamId> incoming_bidirectional_streams_;
  quiche::QuicheCircularDeque<QuicStreamId> incoming_unidirectional_streams_;

  bool close_sent_ = false;
  bool close_received_ = false;
  bool close_notified_ = false;

  quiche::SingleUseCallback<void()> drain_callback_ = nullptr;

  WebTransportHttp3RejectionReason rejection_reason_ =
      WebTransportHttp3RejectionReason::kNone;
  bool drain_sent_ = false;
  // Those are set to default values, which are used if the session is not
  // closed cleanly using an appropriate capsule.
  WebTransportSessionError error_code_ = 0;
  std::string error_message_ = "";
};

class QUICHE_EXPORT WebTransportHttp3UnidirectionalStream : public QuicStream {
 public:
  // Incoming stream.
  WebTransportHttp3UnidirectionalStream(PendingStream* pending,
                                        QuicSpdySession* session);
  // Outgoing stream.
  WebTransportHttp3UnidirectionalStream(QuicStreamId id,
                                        QuicSpdySession* session,
                                        WebTransportSessionId session_id);

  // Sends the stream type and the session ID on the stream.
  void WritePreamble();

  // Implementation of QuicStream.
  void OnDataAvailable() override;
  void OnCanWriteNewData() override;
  void OnClose() override;
  void OnStreamReset(const QuicRstStreamFrame& frame) override;
  bool OnStopSending(QuicResetStreamError error) override;
  void OnWriteSideInDataRecvdState() override;

  WebTransportStream* interface() { return &adapter_; }
  void SetUnblocked() { sequencer()->SetUnblocked(); }

 private:
  QuicSpdySession* session_;
  WebTransportStreamAdapter adapter_;
  std::optional<WebTransportSessionId> session_id_;
  bool needs_to_send_preamble_;

  bool ReadSessionId();
  // Closes the stream if all of the data has been received.
  void MaybeCloseIncompleteStream();
};

// Remaps HTTP/3 error code into a WebTransport error code.  Returns nullopt if
// the provided code is outside of valid range.
QUICHE_EXPORT std::optional<WebTransportStreamError> Http3ErrorToWebTransport(
    uint64_t http3_error_code);

// Same as above, but returns default error value (zero) when none could be
// mapped.
QUICHE_EXPORT WebTransportStreamError
Http3ErrorToWebTransportOrDefault(uint64_t http3_error_code);

// Remaps WebTransport error code into an HTTP/3 error code.
QUICHE_EXPORT uint64_t
WebTransportErrorToHttp3(WebTransportStreamError webtransport_error_code);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_HTTP3_H_
