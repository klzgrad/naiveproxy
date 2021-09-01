// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_HTTP3_H_
#define QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_HTTP3_H_

#include <memory>

#include "absl/container/flat_hash_set.h"
#include "absl/types/optional.h"
#include "quic/core/http/quic_spdy_session.h"
#include "quic/core/quic_stream.h"
#include "quic/core/quic_types.h"
#include "quic/core/web_transport_interface.h"
#include "quic/core/web_transport_stream_adapter.h"
#include "spdy/core/spdy_header_block.h"

namespace quic {

class QuicSpdySession;
class QuicSpdyStream;

// A session of WebTransport over HTTP/3.  The session is owned by
// QuicSpdyStream object for the CONNECT stream that established it.
//
// WebTransport over HTTP/3 specification:
// <https://datatracker.ietf.org/doc/html/draft-ietf-webtrans-http3>
class QUIC_EXPORT_PRIVATE WebTransportHttp3
    : public WebTransportSession,
      public QuicSpdyStream::Http3DatagramRegistrationVisitor,
      public QuicSpdyStream::Http3DatagramVisitor {
 public:
  WebTransportHttp3(QuicSpdySession* session, QuicSpdyStream* connect_stream,
                    WebTransportSessionId id);

  void HeadersReceived(const spdy::SpdyHeaderBlock& headers);
  void SetVisitor(std::unique_ptr<WebTransportVisitor> visitor) {
    visitor_ = std::move(visitor);
  }

  WebTransportSessionId id() { return id_; }
  bool ready() { return ready_; }
  absl::optional<QuicDatagramContextId> context_id() const {
    return context_id_;
  }

  void AssociateStream(QuicStreamId stream_id);
  void OnStreamClosed(QuicStreamId stream_id) { streams_.erase(stream_id); }
  void CloseAllAssociatedStreams();

  size_t NumberOfAssociatedStreams() { return streams_.size(); }

  // Return the earliest incoming stream that has been received by the session
  // but has not been accepted.  Returns nullptr if there are no incoming
  // streams.
  WebTransportStream* AcceptIncomingBidirectionalStream() override;
  WebTransportStream* AcceptIncomingUnidirectionalStream() override;

  bool CanOpenNextOutgoingBidirectionalStream() override;
  bool CanOpenNextOutgoingUnidirectionalStream() override;
  WebTransportStream* OpenOutgoingBidirectionalStream() override;
  WebTransportStream* OpenOutgoingUnidirectionalStream() override;

  MessageStatus SendOrQueueDatagram(QuicMemSlice datagram) override;
  void SetDatagramMaxTimeInQueue(QuicTime::Delta max_time_in_queue) override;

  // From QuicSpdyStream::Http3DatagramVisitor.
  void OnHttp3Datagram(QuicStreamId stream_id,
                       absl::optional<QuicDatagramContextId> context_id,
                       absl::string_view payload) override;

  // From QuicSpdyStream::Http3DatagramRegistrationVisitor.
  void OnContextReceived(
      QuicStreamId stream_id, absl::optional<QuicDatagramContextId> context_id,
      const Http3DatagramContextExtensions& extensions) override;
  void OnContextClosed(
      QuicStreamId stream_id, absl::optional<QuicDatagramContextId> context_id,
      const Http3DatagramContextExtensions& extensions) override;

 private:
  QuicSpdySession* const session_;        // Unowned.
  QuicSpdyStream* const connect_stream_;  // Unowned.
  const WebTransportSessionId id_;
  absl::optional<QuicDatagramContextId> context_id_;
  // |ready_| is set to true when the peer has seen both sets of headers.
  bool ready_ = false;
  // Whether we know which |context_id_| to use. On the client this is always
  // true, and on the server it becomes true when we receive a context
  // registeration capsule.
  bool context_is_known_ = false;
  // Whether |context_id_| is currently registered with |connect_stream_|.
  bool context_currently_registered_ = false;
  std::unique_ptr<WebTransportVisitor> visitor_;
  absl::flat_hash_set<QuicStreamId> streams_;
  quiche::QuicheCircularDeque<QuicStreamId> incoming_bidirectional_streams_;
  quiche::QuicheCircularDeque<QuicStreamId> incoming_unidirectional_streams_;
};

class QUIC_EXPORT_PRIVATE WebTransportHttp3UnidirectionalStream
    : public QuicStream {
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

  WebTransportStream* interface() { return &adapter_; }
  void SetUnblocked() { sequencer()->SetUnblocked(); }

 private:
  QuicSpdySession* session_;
  WebTransportStreamAdapter adapter_;
  absl::optional<WebTransportSessionId> session_id_;
  bool needs_to_send_preamble_;

  bool ReadSessionId();
  // Closes the stream if all of the data has been received.
  void MaybeCloseIncompleteStream();
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_HTTP3_H_
