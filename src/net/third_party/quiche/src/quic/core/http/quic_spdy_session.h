// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_SESSION_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_SESSION_H_

#include <cstddef>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/http/http_frames.h"
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
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
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

class QUIC_EXPORT_PRIVATE Http3DebugVisitor {
 public:
  Http3DebugVisitor();
  Http3DebugVisitor(const Http3DebugVisitor&) = delete;
  Http3DebugVisitor& operator=(const Http3DebugVisitor&) = delete;

  virtual ~Http3DebugVisitor();

  // TODO(https://crbug.com/1062700): Remove default implementation of all
  // methods after Chrome's QuicHttp3Logger has overrides.  This is to make sure
  // QUICHE merge is not blocked on having to add those overrides, they can
  // happen asynchronously.

  // Creation of unidirectional streams.

  // Called when locally-initiated control stream is created.
  virtual void OnControlStreamCreated(QuicStreamId /*stream_id*/) {}
  // Called when locally-initiated QPACK encoder stream is created.
  virtual void OnQpackEncoderStreamCreated(QuicStreamId /*stream_id*/) {}
  // Called when locally-initiated QPACK decoder stream is created.
  virtual void OnQpackDecoderStreamCreated(QuicStreamId /*stream_id*/) {}
  // Called when peer's control stream type is received.
  virtual void OnPeerControlStreamCreated(QuicStreamId /*stream_id*/) = 0;
  // Called when peer's QPACK encoder stream type is received.
  virtual void OnPeerQpackEncoderStreamCreated(QuicStreamId /*stream_id*/) = 0;
  // Called when peer's QPACK decoder stream type is received.
  virtual void OnPeerQpackDecoderStreamCreated(QuicStreamId /*stream_id*/) = 0;

  // Incoming HTTP/3 frames on the control stream.
  virtual void OnCancelPushFrameReceived(const CancelPushFrame& /*frame*/) {}
  virtual void OnSettingsFrameReceived(const SettingsFrame& /*frame*/) = 0;
  virtual void OnGoAwayFrameReceived(const GoAwayFrame& /*frame*/) {}
  virtual void OnMaxPushIdFrameReceived(const MaxPushIdFrame& /*frame*/) {}
  virtual void OnPriorityUpdateFrameReceived(
      const PriorityUpdateFrame& /*frame*/) {}

  // Incoming HTTP/3 frames on request or push streams.
  virtual void OnDataFrameReceived(QuicStreamId /*stream_id*/,
                                   QuicByteCount /*payload_length*/) {}
  virtual void OnHeadersFrameReceived(
      QuicStreamId /*stream_id*/,
      QuicByteCount /*compressed_headers_length*/) {}
  virtual void OnHeadersDecoded(QuicStreamId /*stream_id*/,
                                QuicHeaderList /*headers*/) {}
  virtual void OnPushPromiseFrameReceived(QuicStreamId /*stream_id*/,
                                          QuicStreamId /*push_id*/,
                                          QuicByteCount
                                          /*compressed_headers_length*/) {}
  virtual void OnPushPromiseDecoded(QuicStreamId /*stream_id*/,
                                    QuicStreamId /*push_id*/,
                                    QuicHeaderList /*headers*/) {}

  // Incoming HTTP/3 frames of unknown type on any stream.
  virtual void OnUnknownFrameReceived(QuicStreamId /*stream_id*/,
                                      uint64_t /*frame_type*/,
                                      QuicByteCount /*payload_length*/) {}

  // Outgoing HTTP/3 frames on the control stream.
  virtual void OnSettingsFrameSent(const SettingsFrame& /*frame*/) = 0;
  virtual void OnGoAwayFrameSent(QuicStreamId /*stream_id*/) {}
  virtual void OnMaxPushIdFrameSent(const MaxPushIdFrame& /*frame*/) {}
  virtual void OnPriorityUpdateFrameSent(const PriorityUpdateFrame& /*frame*/) {
  }

  // Outgoing HTTP/3 frames on request or push streams.
  virtual void OnDataFrameSent(QuicStreamId /*stream_id*/,
                               QuicByteCount /*payload_length*/) {}
  virtual void OnHeadersFrameSent(
      QuicStreamId /*stream_id*/,
      const spdy::SpdyHeaderBlock& /*header_block*/) {}
  virtual void OnPushPromiseFrameSent(
      QuicStreamId /*stream_id*/,
      QuicStreamId
      /*push_id*/,
      const spdy::SpdyHeaderBlock& /*header_block*/) {}
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
  void OnDecoderStreamError(quiche::QuicheStringPiece error_message) override;

  // QpackDecoder::EncoderStreamErrorDelegate implementation.
  void OnEncoderStreamError(quiche::QuicheStringPiece error_message) override;

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

  // Called when an HTTP/3 PRIORITY_UPDATE frame has been received for a request
  // stream.  Returns false and closes connection if |stream_id| is invalid.
  bool OnPriorityUpdateForRequestStream(QuicStreamId stream_id, int urgency);

  // Called when an HTTP/3 PRIORITY_UPDATE frame has been received for a push
  // stream.  Returns false and closes connection if |push_id| is invalid.
  bool OnPriorityUpdateForPushStream(QuicStreamId push_id, int urgency);

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

  // Writes an HTTP/2 PRIORITY frame the to peer. Returns the size in bytes of
  // the resulting PRIORITY frame for QUIC_VERSION_43 and above. Otherwise, does
  // nothing and returns 0.
  size_t WritePriority(QuicStreamId id,
                       QuicStreamId parent_stream_id,
                       int weight,
                       bool exclusive);

  // Writes an HTTP/3 PRIORITY_UPDATE frame to the peer.
  void WriteHttp3PriorityUpdate(const PriorityUpdateFrame& priority_update);

  // Process received HTTP/3 GOAWAY frame. This method should only be called on
  // the client side.
  virtual void OnHttp3GoAway(QuicStreamId stream_id);

  // Send GOAWAY if the peer is blocked on the implementation max.
  bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame) override;

  // Write the GOAWAY |frame| on control stream.
  void SendHttp3GoAway();

  // Write |headers| for |promised_stream_id| on |original_stream_id| in a
  // PUSH_PROMISE frame to peer.
  virtual void WritePushPromise(QuicStreamId original_stream_id,
                                QuicStreamId promised_stream_id,
                                spdy::SpdyHeaderBlock headers);

  QpackEncoder* qpack_encoder();
  QpackDecoder* qpack_decoder();
  QuicHeadersStream* headers_stream() { return headers_stream_; }

  const QuicHeadersStream* headers_stream() const { return headers_stream_; }

  // Returns whether server push is enabled.
  // For a Google QUIC client this always returns false.
  // For a Google QUIC server this is set by incoming SETTINGS_ENABLE_PUSH.
  // For an IETF QUIC client this returns true if SetMaxPushId() has ever been
  // called.
  // For an IETF QUIC server this returns true if EnableServerPush() has been
  // called and the server has received at least one MAX_PUSH_ID frame from the
  // client.
  bool server_push_enabled() const;

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

  // Sets |max_push_id_| and sends a MAX_PUSH_ID frame.
  // This method must only be called if protocol is IETF QUIC and perspective is
  // client.  |max_push_id| must be greater than or equal to current
  // |max_push_id_|.
  void SetMaxPushId(QuicStreamId max_push_id);

  // Sets |max_push_id_|.
  // This method must only be called if protocol is IETF QUIC and perspective is
  // server.  It must only be called if a MAX_PUSH_ID frame is received.
  // Returns whether |max_push_id| is greater than or equal to current
  // |max_push_id_|.
  bool OnMaxPushIdFrame(QuicStreamId max_push_id);

  // Enables server push.
  // Must only be called when using IETF QUIC, for which server push is disabled
  // by default.  Server push defaults to enabled and cannot be disabled for
  // Google QUIC.
  // Must only be called for a server.  A client can effectively disable push by
  // never calling SetMaxPushId().
  void EnableServerPush();

  // Returns true if push is enabled and a push with |push_id| can be created.
  // For a server this means that EnableServerPush() has been called, at least
  // one MAX_PUSH_ID frame has been received, and the largest received
  // MAX_PUSH_ID value is greater than or equal to |push_id|.
  // For a client this means that SetMaxPushId() has been called with
  // |max_push_id| greater than or equal to |push_id|.
  // Must only be called when using IETF QUIC.
  // TODO(b/136295430): Use sequential PUSH IDs instead of stream IDs.
  bool CanCreatePushStreamWithId(QuicStreamId push_id);

  int32_t destruction_indicator() const { return destruction_indicator_; }

  void set_debug_visitor(Http3DebugVisitor* debug_visitor) {
    debug_visitor_ = debug_visitor;
  }

  Http3DebugVisitor* debug_visitor() { return debug_visitor_; }

  bool http3_goaway_received() const { return http3_goaway_received_; }

  bool http3_goaway_sent() const { return http3_goaway_sent_; }

  // Log header compression ratio histogram.
  // |using_qpack| is true for QPACK, false for HPACK.
  // |is_sent| is true for sent headers, false for received ones.
  // Ratio is recorded as percentage.  Smaller value means more efficient
  // compression.  Compressed size might be larger than uncompressed size, but
  // recorded ratio is trunckated at 200%.
  // Uncompressed size can be zero for an empty header list, and compressed size
  // can be zero for an empty header list when using HPACK.  (QPACK always emits
  // a header block prefix of at least two bytes.)  This method records nothing
  // if either |compressed| or |uncompressed| is not positive.
  // In order for measurements for different protocol to be comparable, the
  // caller must ensure that uncompressed size is the total length of header
  // names and values without any overhead.
  static void LogHeaderCompressionRatioHistogram(bool using_qpack,
                                                 bool is_sent,
                                                 QuicByteCount compressed,
                                                 QuicByteCount uncompressed);

  // True if any dynamic table entries have been referenced from either a sent
  // or received header block.  Used for stats.
  bool dynamic_table_entry_referenced() const {
    return (qpack_encoder_ &&
            qpack_encoder_->dynamic_table_entry_referenced()) ||
           (qpack_decoder_ && qpack_decoder_->dynamic_table_entry_referenced());
  }

  void OnStreamCreated(QuicSpdyStream* stream);

 protected:
  // Override CreateIncomingStream(), CreateOutgoingBidirectionalStream() and
  // CreateOutgoingUnidirectionalStream() with QuicSpdyStream return type to
  // make sure that all data streams are QuicSpdyStreams.
  QuicSpdyStream* CreateIncomingStream(QuicStreamId id) override = 0;
  QuicSpdyStream* CreateIncomingStream(PendingStream* pending) override = 0;
  virtual QuicSpdyStream* CreateOutgoingBidirectionalStream() = 0;
  virtual QuicSpdyStream* CreateOutgoingUnidirectionalStream() = 0;

  QuicSpdyStream* GetOrCreateSpdyDataStream(const QuicStreamId stream_id);

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

  void OnNewEncryptionKeyAvailable(
      EncryptionLevel level,
      std::unique_ptr<QuicEncrypter> encrypter) override;

  void SetDefaultEncryptionLevel(quic::EncryptionLevel level) override;
  void OnOneRttKeysAvailable() override;

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

  void SendMaxPushId();

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
      quiche::QuicheStringPiece type);

  // Sends any data which should be sent at the start of a connection,
  // including the initial SETTINGS frame, etc.
  void SendInitialData();

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
  // |qpack_maximum_dynamic_table_capacity_| also serves as an upper bound for
  // the dynamic table capacity of the encoding context, to limit memory usage
  // if a larger SETTINGS_QPACK_MAX_TABLE_CAPACITY value is received.
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
  // TODO(b/148616439): Honor this field when sending headers.
  size_t max_outbound_header_list_size_;

  // Data about the stream whose headers are being processed.
  QuicStreamId stream_id_;
  QuicStreamId promised_stream_id_;
  bool fin_;
  size_t frame_len_;

  spdy::SpdyFramer spdy_framer_;
  http2::Http2DecoderAdapter h2_deframer_;
  std::unique_ptr<SpdyFramerVisitor> spdy_framer_visitor_;

  // Used in Google QUIC only.  Set every time SETTINGS_ENABLE_PUSH is received.
  // Defaults to true.
  bool server_push_enabled_;

  // Used in IETF QUIC only.  Defaults to false.
  // Server push is enabled for a server by calling EnableServerPush().
  // Server push is enabled for a client by calling SetMaxPushId().
  bool ietf_server_push_enabled_;

  // Used in IETF QUIC only.  Unset until a MAX_PUSH_ID frame is received/sent.
  // For a server, the push ID in the most recently received MAX_PUSH_ID frame.
  // For a client before 1-RTT keys are available, the push ID to be sent in the
  // initial MAX_PUSH_ID frame.
  // For a client after 1-RTT keys are available, the push ID in the most
  // recently sent MAX_PUSH_ID frame.
  quiche::QuicheOptional<QuicStreamId> max_push_id_;

  // An integer used for live check. The indicator is assigned a value in
  // constructor. As long as it is not the assigned value, that would indicate
  // an use-after-free.
  int32_t destruction_indicator_;

  // Not owned by the session.
  Http3DebugVisitor* debug_visitor_;

  // If the endpoint has received HTTP/3 GOAWAY frame.
  bool http3_goaway_received_;
  // If the endpoint has sent HTTP/3 GOAWAY frame.
  bool http3_goaway_sent_;

  // If SendMaxPushId() has been called from SendInitialData().  Note that a
  // MAX_PUSH_ID frame is only sent if SetMaxPushId() had been called
  // beforehand.
  bool http3_max_push_id_sent_;

  // Priority values received in PRIORITY_UPDATE frames for streams that are not
  // open yet.
  QuicUnorderedMap<QuicStreamId, int> buffered_stream_priorities_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_SESSION_H_
