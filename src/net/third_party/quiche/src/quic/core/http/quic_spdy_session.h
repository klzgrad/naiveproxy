// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_SESSION_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_SESSION_H_

#include <cstddef>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/http/quic_header_list.h"
#include "net/third_party/quiche/src/quic/core/http/quic_headers_stream.h"
#include "net/third_party/quiche/src/quic/core/http/quic_receive_control_stream.h"
#include "net/third_party/quiche/src/quic/core/http/quic_send_control_stream.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_stream_sender.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_stream_sender.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_receive_stream.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_send_stream.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/http2_frame_decoder_adapter.h"

namespace quic {

namespace test {
class QuicSpdySessionPeer;
}  // namespace test

// QuicHpackDebugVisitor gathers data used for understanding HPACK HoL
// dynamics.  Specifically, it is to help predict the compression
// penalty of avoiding HoL by chagning how the dynamic table is used.
// In chromium, the concrete instance populates an UMA
// histogram with the data.
class QUIC_EXPORT_PRIVATE QuicHpackDebugVisitor {
 public:
  QuicHpackDebugVisitor();
  QuicHpackDebugVisitor(const QuicHpackDebugVisitor&) = delete;
  QuicHpackDebugVisitor& operator=(const QuicHpackDebugVisitor&) = delete;

  virtual ~QuicHpackDebugVisitor();

  // For each HPACK indexed representation processed, |elapsed| is
  // the time since the corresponding entry was added to the dynamic
  // table.
  virtual void OnUseEntry(QuicTime::Delta elapsed) = 0;
};

// A QUIC session for HTTP.
class QUIC_EXPORT_PRIVATE QuicSpdySession
    : public QuicSession,
      public QpackEncoder::DecoderStreamErrorDelegate,
      public QpackDecoder::EncoderStreamErrorDelegate {
 public:
  // Does not take ownership of |connection| or |visitor|.
  QuicSpdySession(QuicConnection* connection,
                  QuicSession::Visitor* visitor,
                  const QuicConfig& config,
                  const ParsedQuicVersionVector& supported_versions);
  QuicSpdySession(const QuicSpdySession&) = delete;
  QuicSpdySession& operator=(const QuicSpdySession&) = delete;

  ~QuicSpdySession() override;

  void Initialize() override;

  // QpackEncoder::DecoderStreamErrorDelegate implementation.
  void OnDecoderStreamError(QuicStringPiece error_message) override;

  // QpackDecoder::EncoderStreamErrorDelegate implementation.
  void OnEncoderStreamError(QuicStringPiece error_message) override;

  // Called by |headers_stream_| when headers with a priority have been
  // received for a stream.  This method will only be called for server streams.
  virtual void OnStreamHeadersPriority(
      QuicStreamId stream_id,
      const spdy::SpdyStreamPrecedence& precedence);

  // Called by |headers_stream_| when headers have been completely received
  // for a stream.  |fin| will be true if the fin flag was set in the headers
  // frame.
  virtual void OnStreamHeaderList(QuicStreamId stream_id,
                                  bool fin,
                                  size_t frame_len,
                                  const QuicHeaderList& header_list);

  // Called by |headers_stream_| when push promise headers have been
  // completely received.  |fin| will be true if the fin flag was set
  // in the headers.
  virtual void OnPromiseHeaderList(QuicStreamId stream_id,
                                   QuicStreamId promised_stream_id,
                                   size_t frame_len,
                                   const QuicHeaderList& header_list);

  // Called by |headers_stream_| when a PRIORITY frame has been received for a
  // stream. This method will only be called for server streams.
  virtual void OnPriorityFrame(QuicStreamId stream_id,
                               const spdy::SpdyStreamPrecedence& precedence);

  // Sends contents of |iov| to h2_deframer_, returns number of bytes processed.
  size_t ProcessHeaderData(const struct iovec& iov);

  // Writes |headers| for the stream |id| to the dedicated headers stream.
  // If |fin| is true, then no more data will be sent for the stream |id|.
  // If provided, |ack_notifier_delegate| will be registered to be notified when
  // we have seen ACKs for all packets resulting from this call.
  virtual size_t WriteHeadersOnHeadersStream(
      QuicStreamId id,
      spdy::SpdyHeaderBlock headers,
      bool fin,
      const spdy::SpdyStreamPrecedence& precedence,
      QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener);

  // Writes a PRIORITY frame the to peer. Returns the size in bytes of the
  // resulting PRIORITY frame for QUIC_VERSION_43 and above. Otherwise, does
  // nothing and returns 0.
  size_t WritePriority(QuicStreamId id,
                       QuicStreamId parent_stream_id,
                       int weight,
                       bool exclusive);

  // Writes a HTTP/3 PRIORITY frame to the peer.
  void WriteH3Priority(const PriorityFrame& priority);

  // Write |headers| for |promised_stream_id| on |original_stream_id| in a
  // PUSH_PROMISE frame to peer.
  virtual void WritePushPromise(QuicStreamId original_stream_id,
                                QuicStreamId promised_stream_id,
                                spdy::SpdyHeaderBlock headers);

  // Sends SETTINGS_MAX_HEADER_LIST_SIZE SETTINGS frame.
  void SendMaxHeaderListSize(size_t value);

  QpackEncoder* qpack_encoder();
  QpackDecoder* qpack_decoder();
  QuicHeadersStream* headers_stream() { return headers_stream_; }

  const QuicHeadersStream* headers_stream() const { return headers_stream_; }

  bool server_push_enabled() const { return server_push_enabled_; }

  // Called when a setting is parsed from an incoming SETTINGS frame.
  void OnSetting(uint64_t id, uint64_t value);

  // Return true if this session wants to release headers stream's buffer
  // aggressively.
  virtual bool ShouldReleaseHeadersStreamSequencerBuffer();

  void CloseConnectionWithDetails(QuicErrorCode error,
                                  const std::string& details);

  // Must not be called after Initialize().
  // TODO(bnc): Move to constructor argument.
  void set_qpack_maximum_dynamic_table_capacity(
      uint64_t qpack_maximum_dynamic_table_capacity) {
    qpack_maximum_dynamic_table_capacity_ =
        qpack_maximum_dynamic_table_capacity;
  }

  // Must not be called after Initialize().
  // TODO(bnc): Move to constructor argument.
  void set_qpack_maximum_blocked_streams(
      uint64_t qpack_maximum_blocked_streams) {
    qpack_maximum_blocked_streams_ = qpack_maximum_blocked_streams;
  }

  // Must not be called after Initialize().
  // TODO(bnc): Move to constructor argument.
  void set_max_inbound_header_list_size(size_t max_inbound_header_list_size) {
    max_inbound_header_list_size_ = max_inbound_header_list_size;
  }

  size_t max_outbound_header_list_size() const {
    return max_outbound_header_list_size_;
  }

  size_t max_inbound_header_list_size() const {
    return max_inbound_header_list_size_;
  }

  // Returns true if the session has active request streams.
  bool HasActiveRequestStreams() const;

  // Called when the size of the compressed frame payload is available.
  void OnCompressedFrameSize(size_t frame_len);

  // Called when a PUSH_PROMISE frame has been received.
  void OnPushPromise(spdy::SpdyStreamId stream_id,
                     spdy::SpdyStreamId promised_stream_id);

  // Called when the complete list of headers is available.
  void OnHeaderList(const QuicHeaderList& header_list);

  QuicStreamId promised_stream_id() const { return promised_stream_id_; }

  // Initialze HTTP/3 unidirectional streams if |unidirectional| is true and
  // those streams are not initialized yet.
  void OnCanCreateNewOutgoingStream(bool unidirectional) override;

  void set_max_allowed_push_id(QuicStreamId max_allowed_push_id);

  QuicStreamId max_allowed_push_id() { return max_allowed_push_id_; }

  int32_t destruction_indicator() const { return destruction_indicator_; }

 protected:
  // Override CreateIncomingStream(), CreateOutgoingBidirectionalStream() and
  // CreateOutgoingUnidirectionalStream() with QuicSpdyStream return type to
  // make sure that all data streams are QuicSpdyStreams.
  QuicSpdyStream* CreateIncomingStream(QuicStreamId id) override = 0;
  QuicSpdyStream* CreateIncomingStream(PendingStream* pending) override = 0;
  virtual QuicSpdyStream* CreateOutgoingBidirectionalStream() = 0;
  virtual QuicSpdyStream* CreateOutgoingUnidirectionalStream() = 0;

  QuicSpdyStream* GetSpdyDataStream(const QuicStreamId stream_id);

  // If an incoming stream can be created, return true.
  virtual bool ShouldCreateIncomingStream(QuicStreamId id) = 0;

  // If an outgoing bidirectional/unidirectional stream can be created, return
  // true.
  virtual bool ShouldCreateOutgoingBidirectionalStream() = 0;
  virtual bool ShouldCreateOutgoingUnidirectionalStream() = 0;

  // Returns true if there are open HTTP requests.
  bool ShouldKeepConnectionAlive() const override;

  // Overridden to buffer incoming unidirectional streams for version 99.
  bool UsesPendingStreams() const override;

  // Overridden to Process HTTP/3 stream types. H/3 streams will be created from
  // pending streams accordingly if the stream type can be read. Returns true if
  // unidirectional streams are created.
  bool ProcessPendingStream(PendingStream* pending) override;

  size_t WriteHeadersOnHeadersStreamImpl(
      QuicStreamId id,
      spdy::SpdyHeaderBlock headers,
      bool fin,
      QuicStreamId parent_stream_id,
      int weight,
      bool exclusive,
      QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener);

  void OnCryptoHandshakeEvent(CryptoHandshakeEvent event) override;

  bool supports_push_promise() { return supports_push_promise_; }

  // Optional, enables instrumentation related to go/quic-hpack.
  void SetHpackEncoderDebugVisitor(
      std::unique_ptr<QuicHpackDebugVisitor> visitor);
  void SetHpackDecoderDebugVisitor(
      std::unique_ptr<QuicHpackDebugVisitor> visitor);

  // Sets the maximum size of the header compression table spdy_framer_ is
  // willing to use to encode header blocks.
  void UpdateHeaderEncoderTableSize(uint32_t value);

  // Called when SETTINGS_ENABLE_PUSH is received, only supported on
  // server side.
  void UpdateEnableServerPush(bool value);

  bool IsConnected() { return connection()->connected(); }

  const QuicReceiveControlStream* receive_control_stream() const {
    return receive_control_stream_;
  }

  // Initializes HTTP/3 unidirectional streams if not yet initialzed.
  virtual void MaybeInitializeHttp3UnidirectionalStreams();

  void set_max_uncompressed_header_bytes(
      size_t set_max_uncompressed_header_bytes);

  void SendMaxPushId(QuicStreamId max_allowed_push_id);

 private:
  friend class test::QuicSpdySessionPeer;

  class SpdyFramerVisitor;

  // The following methods are called by the SimpleVisitor.

  // Called when a HEADERS frame has been received.
  void OnHeaders(spdy::SpdyStreamId stream_id,
                 bool has_priority,
                 const spdy::SpdyStreamPrecedence& precedence,
                 bool fin);

  // Called when a PRIORITY frame has been received.
  void OnPriority(spdy::SpdyStreamId stream_id,
                  const spdy::SpdyStreamPrecedence& precedence);

  void CloseConnectionOnDuplicateHttp3UnidirectionalStreams(
      QuicStringPiece type);

  std::unique_ptr<QpackEncoder> qpack_encoder_;
  std::unique_ptr<QpackDecoder> qpack_decoder_;

  // Pointer to the header stream in stream_map_.
  QuicHeadersStream* headers_stream_;

  // HTTP/3 control streams. They are owned by QuicSession inside
  // stream map, and can be accessed by those unowned pointers below.
  QuicSendControlStream* send_control_stream_;
  QuicReceiveControlStream* receive_control_stream_;

  // Pointers to HTTP/3 QPACK streams in stream map.
  QpackReceiveStream* qpack_encoder_receive_stream_;
  QpackReceiveStream* qpack_decoder_receive_stream_;
  QpackSendStream* qpack_encoder_send_stream_;
  QpackSendStream* qpack_decoder_send_stream_;

  // Maximum dynamic table capacity as defined at
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#maximum-dynamic-table-capacity
  // for the decoding context.  Value will be sent via
  // SETTINGS_QPACK_MAX_TABLE_CAPACITY.
  uint64_t qpack_maximum_dynamic_table_capacity_;

  // Maximum number of blocked streams as defined at
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#blocked-streams
  // for the decoding context.  Value will be sent via
  // SETTINGS_QPACK_BLOCKED_STREAMS.
  uint64_t qpack_maximum_blocked_streams_;

  // The maximum size of a header block that will be accepted from the peer,
  // defined per spec as key + value + overhead per field (uncompressed).
  // Value will be sent via SETTINGS_MAX_HEADER_LIST_SIZE.
  size_t max_inbound_header_list_size_;

  // The maximum size of a header block that can be sent to the peer. This field
  // is informed and set by the peer via SETTINGS frame.
  // TODO(renjietang): Honor this field when sending headers.
  size_t max_outbound_header_list_size_;

  // Set during handshake. If true, resources in x-associated-content and link
  // headers will be pushed.
  bool server_push_enabled_;

  // Data about the stream whose headers are being processed.
  QuicStreamId stream_id_;
  QuicStreamId promised_stream_id_;
  bool fin_;
  size_t frame_len_;

  bool supports_push_promise_;

  spdy::SpdyFramer spdy_framer_;
  http2::Http2DecoderAdapter h2_deframer_;
  std::unique_ptr<SpdyFramerVisitor> spdy_framer_visitor_;
  QuicStreamId max_allowed_push_id_;

  // An integer used for live check. The indicator is assigned a value in
  // constructor. As long as it is not the assigned value, that would indicate
  // an use-after-free.
  int32_t destruction_indicator_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_SESSION_H_
