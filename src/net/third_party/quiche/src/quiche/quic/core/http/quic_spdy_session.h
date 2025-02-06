// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_SESSION_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_SESSION_H_

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/http2/core/http2_frame_decoder_adapter.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/http/quic_headers_stream.h"
#include "quiche/quic/core/http/quic_receive_control_stream.h"
#include "quiche/quic/core/http/quic_send_control_stream.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/qpack/qpack_decoder.h"
#include "quiche/quic/core/qpack/qpack_encoder.h"
#include "quiche/quic/core/qpack/qpack_receive_stream.h"
#include "quiche/quic/core/qpack/qpack_send_stream.h"
#include "quiche/quic/core/qpack/value_splitting_header_list.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/http/http_header_block.h"

namespace quic {

namespace test {
class QuicSpdySessionPeer;
}  // namespace test

class WebTransportHttp3UnidirectionalStream;

QUICHE_EXPORT extern const size_t kMaxUnassociatedWebTransportStreams;

class QUICHE_EXPORT Http3DebugVisitor {
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
  virtual void OnControlStreamCreated(QuicStreamId /*stream_id*/) = 0;
  // Called when locally-initiated QPACK encoder stream is created.
  virtual void OnQpackEncoderStreamCreated(QuicStreamId /*stream_id*/) = 0;
  // Called when locally-initiated QPACK decoder stream is created.
  virtual void OnQpackDecoderStreamCreated(QuicStreamId /*stream_id*/) = 0;
  // Called when peer's control stream type is received.
  virtual void OnPeerControlStreamCreated(QuicStreamId /*stream_id*/) = 0;
  // Called when peer's QPACK encoder stream type is received.
  virtual void OnPeerQpackEncoderStreamCreated(QuicStreamId /*stream_id*/) = 0;
  // Called when peer's QPACK decoder stream type is received.
  virtual void OnPeerQpackDecoderStreamCreated(QuicStreamId /*stream_id*/) = 0;

  // Incoming HTTP/3 frames in ALPS TLS extension.
  virtual void OnSettingsFrameReceivedViaAlps(const SettingsFrame& /*frame*/) {}
  virtual void OnAcceptChFrameReceivedViaAlps(const AcceptChFrame& /*frame*/) {}

  // Incoming HTTP/3 frames on the control stream.
  virtual void OnSettingsFrameReceived(const SettingsFrame& /*frame*/) = 0;
  virtual void OnGoAwayFrameReceived(const GoAwayFrame& /*frame*/) = 0;
  virtual void OnPriorityUpdateFrameReceived(
      const PriorityUpdateFrame& /*frame*/) = 0;
  virtual void OnOriginFrameReceived(const OriginFrame& /*frame*/) {}
  virtual void OnAcceptChFrameReceived(const AcceptChFrame& /*frame*/) {}

  // Incoming HTTP/3 frames on request or push streams.
  virtual void OnDataFrameReceived(QuicStreamId /*stream_id*/,
                                   QuicByteCount /*payload_length*/) = 0;
  virtual void OnHeadersFrameReceived(
      QuicStreamId /*stream_id*/,
      QuicByteCount /*compressed_headers_length*/) = 0;
  virtual void OnHeadersDecoded(QuicStreamId /*stream_id*/,
                                QuicHeaderList /*headers*/) = 0;

  // Incoming HTTP/3 frames of unknown type on any stream.
  virtual void OnUnknownFrameReceived(QuicStreamId /*stream_id*/,
                                      uint64_t /*frame_type*/,
                                      QuicByteCount /*payload_length*/) = 0;

  // Outgoing HTTP/3 frames on the control stream.
  virtual void OnSettingsFrameSent(const SettingsFrame& /*frame*/) = 0;
  virtual void OnGoAwayFrameSent(QuicStreamId /*stream_id*/) = 0;
  virtual void OnPriorityUpdateFrameSent(
      const PriorityUpdateFrame& /*frame*/) = 0;

  // Outgoing HTTP/3 frames on request or push streams.
  virtual void OnDataFrameSent(QuicStreamId /*stream_id*/,
                               QuicByteCount /*payload_length*/) = 0;
  virtual void OnHeadersFrameSent(
      QuicStreamId /*stream_id*/,
      const quiche::HttpHeaderBlock& /*header_block*/) = 0;

  // 0-RTT related events.
  virtual void OnSettingsFrameResumed(const SettingsFrame& /*frame*/) = 0;
};

// Whether HTTP Datagrams are supported on this session and if so which version
// is currently in use.
enum class HttpDatagramSupport : uint8_t {
  kNone,  // HTTP Datagrams are not supported for this session.
  kDraft04,
  kRfc,
  kRfcAndDraft04,  // Only used locally for sending, we only negotiate one
                   // version.
};

// Versions of WebTransport over HTTP/3 protocol extension.
enum class WebTransportHttp3Version : uint8_t {
  // <https://www.ietf.org/archive/id/draft-ietf-webtrans-http3-02.html>
  // The first version to be ever publicly shipped in Chrome. Sometimes referred
  // to as "draft-00", since draft-02 was backwards-compatible with draft-00.
  kDraft02,
  // <https://www.ietf.org/archive/id/draft-ietf-webtrans-http3-07.html>
  // See the changelog in the appendix for differences between draft-02 and
  // draft-07.
  kDraft07,
};
using WebTransportHttp3VersionSet = BitMask<WebTransportHttp3Version, uint8_t>;

// Note that by default, WebTransport is not enabled. Thus, those are the
// versions primarily used in the tools and unit tests.
inline constexpr WebTransportHttp3VersionSet
    kDefaultSupportedWebTransportVersions =
        WebTransportHttp3VersionSet({WebTransportHttp3Version::kDraft02,
                                     WebTransportHttp3Version::kDraft07});

QUICHE_EXPORT std::string HttpDatagramSupportToString(
    HttpDatagramSupport http_datagram_support);
QUICHE_EXPORT std::ostream& operator<<(
    std::ostream& os, const HttpDatagramSupport& http_datagram_support);

// A QUIC session for HTTP.
class QUICHE_EXPORT QuicSpdySession
    : public QuicSession,
      public QpackEncoder::DecoderStreamErrorDelegate,
      public QpackDecoder::EncoderStreamErrorDelegate {
 public:
  // Does not take ownership of |connection| or |visitor|.
  QuicSpdySession(QuicConnection* connection, QuicSession::Visitor* visitor,
                  const QuicConfig& config,
                  const ParsedQuicVersionVector& supported_versions);
  QuicSpdySession(const QuicSpdySession&) = delete;
  QuicSpdySession& operator=(const QuicSpdySession&) = delete;

  ~QuicSpdySession() override;

  void Initialize() override;

  // QpackEncoder::DecoderStreamErrorDelegate implementation.
  void OnDecoderStreamError(QuicErrorCode error_code,
                            absl::string_view error_message) override;

  // QpackDecoder::EncoderStreamErrorDelegate implementation.
  void OnEncoderStreamError(QuicErrorCode error_code,
                            absl::string_view error_message) override;

  // Called by |headers_stream_| when headers with a priority have been
  // received for a stream.  This method will only be called for server streams.
  virtual void OnStreamHeadersPriority(
      QuicStreamId stream_id, const spdy::SpdyStreamPrecedence& precedence);

  // Called by |headers_stream_| when headers have been completely received
  // for a stream.  |fin| will be true if the fin flag was set in the headers
  // frame.
  virtual void OnStreamHeaderList(QuicStreamId stream_id, bool fin,
                                  size_t frame_len,
                                  const QuicHeaderList& header_list);

  // Called by |headers_stream_| when a PRIORITY frame has been received for a
  // stream. This method will only be called for server streams.
  virtual void OnPriorityFrame(QuicStreamId stream_id,
                               const spdy::SpdyStreamPrecedence& precedence);

  // Called when an HTTP/3 PRIORITY_UPDATE frame has been received for a request
  // stream.  Returns false and closes connection if |stream_id| is invalid.
  bool OnPriorityUpdateForRequestStream(QuicStreamId stream_id,
                                        HttpStreamPriority priority);

  // Called when an HTTP/3 ORIGIN frame has been received.
  // This method will only be called for client sessions.
  virtual void OnOriginFrame(const OriginFrame& /*frame*/) {}

  // Called when an HTTP/3 ACCEPT_CH frame has been received.
  // This method will only be called for client sessions.
  virtual void OnAcceptChFrame(const AcceptChFrame& /*frame*/) {}

  // Called when an HTTP/3 frame of unknown type has been received.
  virtual void OnUnknownFrameStart(QuicStreamId /*stream_id*/,
                                   uint64_t /*frame_type*/,
                                   QuicByteCount /*header_length*/,
                                   QuicByteCount /*payload_length*/) {}
  virtual void OnUnknownFramePayload(QuicStreamId /*stream_id*/,
                                     absl::string_view /*payload*/) {}

  // Sends contents of |iov| to h2_deframer_, returns number of bytes processed.
  size_t ProcessHeaderData(const struct iovec& iov);

  // Writes |headers| for the stream |id| to the dedicated headers stream.
  // If |fin| is true, then no more data will be sent for the stream |id|.
  // If provided, |ack_notifier_delegate| will be registered to be notified when
  // we have seen ACKs for all packets resulting from this call.
  virtual size_t WriteHeadersOnHeadersStream(
      QuicStreamId id, quiche::HttpHeaderBlock headers, bool fin,
      const spdy::SpdyStreamPrecedence& precedence,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener);

  // Writes an HTTP/2 PRIORITY frame the to peer. Returns the size in bytes of
  // the resulting PRIORITY frame.
  size_t WritePriority(QuicStreamId stream_id, QuicStreamId parent_stream_id,
                       int weight, bool exclusive);

  // Writes an HTTP/3 PRIORITY_UPDATE frame to the peer.
  void WriteHttp3PriorityUpdate(QuicStreamId stream_id,
                                HttpStreamPriority priority);

  // Process received HTTP/3 GOAWAY frame.  When sent from server to client,
  // |id| is a stream ID.  When sent from client to server, |id| is a push ID.
  virtual void OnHttp3GoAway(uint64_t id);

  // Send GOAWAY if the peer is blocked on the implementation max.
  bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame) override;

  // Write GOAWAY frame with maximum stream ID on the control stream.  Called to
  // initite graceful connection shutdown.  Do not use smaller stream ID, in
  // case client does not implement retry on GOAWAY.  Do not send GOAWAY if one
  // has already been sent. Send connection close with |error_code| and |reason|
  // before encryption gets established.
  void SendHttp3GoAway(QuicErrorCode error_code, const std::string& reason);

  QpackEncoder* qpack_encoder();
  QpackDecoder* qpack_decoder();
  QuicHeadersStream* headers_stream() { return headers_stream_; }

  const QuicHeadersStream* headers_stream() const { return headers_stream_; }

  // Called when the control stream receives HTTP/3 SETTINGS.
  // Returns false in case of 0-RTT if received settings are incompatible with
  // cached values, true otherwise.
  virtual bool OnSettingsFrame(const SettingsFrame& frame);

  // Called when an HTTP/3 SETTINGS frame is received via ALPS.
  // Returns an error message if an error has occurred, or nullopt otherwise.
  // May or may not close the connection on error.
  std::optional<std::string> OnSettingsFrameViaAlps(const SettingsFrame& frame);

  // Called when a setting is parsed from a SETTINGS frame received on the
  // control stream or from cached application state.
  // Returns true on success.
  // Returns false if received setting is incompatible with cached value (in
  // case of 0-RTT) or with previously received value (in case of ALPS).
  // Also closes the connection on error.
  bool OnSetting(uint64_t id, uint64_t value);

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

  uint64_t qpack_maximum_dynamic_table_capacity() const {
    return qpack_maximum_dynamic_table_capacity_;
  }

  // Must not be called after Initialize().
  // TODO(bnc): Move to constructor argument.
  void set_qpack_maximum_blocked_streams(
      uint64_t qpack_maximum_blocked_streams) {
    qpack_maximum_blocked_streams_ = qpack_maximum_blocked_streams;
  }

  // Should only be used by IETF QUIC server side.
  // Must not be called after Initialize().
  // TODO(bnc): Move to constructor argument.
  void set_max_inbound_header_list_size(size_t max_inbound_header_list_size) {
    max_inbound_header_list_size_ = max_inbound_header_list_size;
  }

  // Must not be called after Initialize().
  void set_allow_extended_connect(bool allow_extended_connect);

  size_t max_outbound_header_list_size() const {
    return max_outbound_header_list_size_;
  }

  size_t max_inbound_header_list_size() const {
    return max_inbound_header_list_size_;
  }

  bool allow_extended_connect() const { return allow_extended_connect_; }

  // Returns true if the session has active request streams.
  bool HasActiveRequestStreams() const;

  // Called when the size of the compressed frame payload is available.
  void OnCompressedFrameSize(size_t frame_len);

  // Called when the complete list of headers is available.
  void OnHeaderList(const QuicHeaderList& header_list);

  // Initialze HTTP/3 unidirectional streams if |unidirectional| is true and
  // those streams are not initialized yet.
  void OnCanCreateNewOutgoingStream(bool unidirectional) override;

  int32_t destruction_indicator() const { return destruction_indicator_; }

  void set_debug_visitor(Http3DebugVisitor* debug_visitor) {
    debug_visitor_ = debug_visitor;
  }

  Http3DebugVisitor* debug_visitor() { return debug_visitor_; }

  // When using Google QUIC, return whether a transport layer GOAWAY frame has
  // been received or sent.
  // When using IETF QUIC, return whether an HTTP/3 GOAWAY frame has been
  // received or sent.
  bool goaway_received() const;
  bool goaway_sent() const;
  std::optional<uint64_t> last_received_http3_goaway_id() {
    return last_received_http3_goaway_id_;
  }

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
  static void LogHeaderCompressionRatioHistogram(bool using_qpack, bool is_sent,
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

  // Decode SETTINGS from |cached_state| and apply it to the session.
  bool ResumeApplicationState(ApplicationState* cached_state) override;

  std::optional<std::string> OnAlpsData(const uint8_t* alps_data,
                                        size_t alps_length) override;

  // Called when ACCEPT_CH frame is parsed out of data received in TLS ALPS
  // extension.
  virtual void OnAcceptChFrameReceivedViaAlps(const AcceptChFrame& /*frame*/);

  // Whether HTTP datagrams are supported on this session and which draft is in
  // use, based on received SETTINGS.
  HttpDatagramSupport http_datagram_support() const {
    return http_datagram_support_;
  }

  // This must not be used except by QuicSpdyStream::SendHttp3Datagram.
  MessageStatus SendHttp3Datagram(QuicStreamId stream_id,
                                  absl::string_view payload);
  // This must not be used except by QuicSpdyStream::SetMaxDatagramTimeInQueue.
  void SetMaxDatagramTimeInQueueForStreamId(QuicStreamId stream_id,
                                            QuicTime::Delta max_time_in_queue);

  // Override from QuicSession to support HTTP/3 datagrams.
  void OnMessageReceived(absl::string_view message) override;

  // Indicates whether the HTTP/3 session supports WebTransport.
  bool SupportsWebTransport();

  // If SupportsWebTransport() is true, returns the version of WebTransport
  // currently in use (which is the highest version supported by both peers).
  std::optional<WebTransportHttp3Version> SupportedWebTransportVersion();

  // Indicates whether both the peer and us support HTTP/3 Datagrams.
  bool SupportsH3Datagram() const;

  // Indicates whether the HTTP/3 session will indicate WebTransport support to
  // the peer.
  bool WillNegotiateWebTransport();

  // Returns a WebTransport session by its session ID.  Returns nullptr if no
  // session is associated with the given ID.
  WebTransportHttp3* GetWebTransportSession(WebTransportSessionId id);

  // If true, no data on bidirectional streams will be processed by the server
  // until the SETTINGS are received.  Only works for HTTP/3. This is currently
  // required either (1) for WebTransport because WebTransport needs settings to
  // correctly parse requests or (2) when multiple versions of HTTP Datagrams
  // are supported to ensure we know which one is used. The HTTP Datagram check
  // will be removed once we drop support for draft04.
  bool ShouldBufferRequestsUntilSettings() {
    return version().UsesHttp3() && perspective() == Perspective::IS_SERVER &&
           (ShouldNegotiateWebTransport() ||
            LocalHttpDatagramSupport() == HttpDatagramSupport::kRfcAndDraft04 ||
            force_buffer_requests_until_settings_);
  }

  // Returns if the incoming bidirectional streams should process data.  This is
  // usually true, but in certain cases we would want to wait until the settings
  // are received.
  bool ShouldProcessIncomingRequests();

  void OnStreamWaitingForClientSettings(QuicStreamId id);

  // Links the specified stream with a WebTransport session.  If the session is
  // not present, it is buffered until a corresponding stream is found.
  void AssociateIncomingWebTransportStreamWithSession(
      WebTransportSessionId session_id, QuicStreamId stream_id);

  void ProcessBufferedWebTransportStreamsForSession(WebTransportHttp3* session);

  bool CanOpenOutgoingUnidirectionalWebTransportStream(
      WebTransportSessionId /*id*/) {
    return CanOpenNextOutgoingUnidirectionalStream();
  }
  bool CanOpenOutgoingBidirectionalWebTransportStream(
      WebTransportSessionId /*id*/) {
    return CanOpenNextOutgoingBidirectionalStream();
  }

  // Creates an outgoing unidirectional WebTransport stream.  Returns nullptr if
  // the stream cannot be created due to flow control or some other reason.
  WebTransportHttp3UnidirectionalStream*
  CreateOutgoingUnidirectionalWebTransportStream(WebTransportHttp3* session);

  // Creates an outgoing bidirectional WebTransport stream.  Returns nullptr if
  // the stream cannot be created due to flow control or some other reason.
  QuicSpdyStream* CreateOutgoingBidirectionalWebTransportStream(
      WebTransportHttp3* session);

  QuicSpdyStream* GetOrCreateSpdyDataStream(const QuicStreamId stream_id);

  // Returns a pointer to the incoming QPACK encoder stream (the one that
  // belongs to the local decoding context). Might return nullptr.
  QpackReceiveStream* GetQpackEncoderReceiveStream() const {
    return qpack_encoder_receive_stream_;
  }

  void OnConfigNegotiated() override;

  // Returns true if the SETTINGS frame has been received from the peer.
  bool settings_received() const { return settings_received_; }

 protected:
  // Override CreateIncomingStream(), CreateOutgoingBidirectionalStream() and
  // CreateOutgoingUnidirectionalStream() with QuicSpdyStream return type to
  // make sure that all data streams are QuicSpdyStreams.
  QuicSpdyStream* CreateIncomingStream(QuicStreamId id) override = 0;
  QuicSpdyStream* CreateIncomingStream(PendingStream* pending) override = 0;
  virtual QuicSpdyStream* CreateOutgoingBidirectionalStream() = 0;
  virtual QuicSpdyStream* CreateOutgoingUnidirectionalStream() = 0;

  // If an incoming stream can be created, return true.
  virtual bool ShouldCreateIncomingStream(QuicStreamId id) = 0;

  // If an outgoing bidirectional/unidirectional stream can be created, return
  // true.
  virtual bool ShouldCreateOutgoingBidirectionalStream() = 0;
  virtual bool ShouldCreateOutgoingUnidirectionalStream() = 0;

  // Indicates whether the underlying backend can accept and process
  // WebTransport sessions over HTTP/3.
  virtual WebTransportHttp3VersionSet LocallySupportedWebTransportVersions()
      const;
  bool ShouldNegotiateWebTransport() const;

  // Returns true if there are open HTTP requests.
  bool ShouldKeepConnectionAlive() const override;

  // Overridden to buffer incoming unidirectional streams for version 99.
  bool UsesPendingStreamForFrame(QuicFrameType type,
                                 QuicStreamId stream_id) const override;

  // Called when a STREAM_FRAME is received on |pending| stream or
  // ProcessAllPendingStreams() gets called. Processes incoming unidirectional
  // streams; parses the stream type, and creates a new stream of the
  // corresponding type. Returns the pointer to the newly created stream, or
  // nullptr if the stream type is not yet available.
  QuicStream* ProcessReadUnidirectionalPendingStream(
      PendingStream* pending) override;

  size_t WriteHeadersOnHeadersStreamImpl(
      QuicStreamId id, quiche::HttpHeaderBlock headers, bool fin,
      QuicStreamId parent_stream_id, int weight, bool exclusive,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener);

  void OnNewEncryptionKeyAvailable(
      EncryptionLevel level, std::unique_ptr<QuicEncrypter> encrypter) override;

  // Sets the maximum size of the header compression table spdy_framer_ is
  // willing to use to encode header blocks.
  void UpdateHeaderEncoderTableSize(uint32_t value);

  bool IsConnected() { return connection()->connected(); }

  const QuicReceiveControlStream* receive_control_stream() const {
    return receive_control_stream_;
  }

  const SettingsFrame& settings() const { return settings_; }

  // Initializes HTTP/3 unidirectional streams if not yet initialzed.
  virtual void MaybeInitializeHttp3UnidirectionalStreams();

  // QuicConnectionVisitorInterface methods.
  void BeforeConnectionCloseSent() override;
  void MaybeBundleOpportunistically() override;

  // Called whenever a datagram is dequeued or dropped from datagram_queue().
  virtual void OnDatagramProcessed(std::optional<MessageStatus> status);

  // Returns which version of the HTTP/3 datagram extension we should advertise
  // in settings and accept remote settings for.
  virtual HttpDatagramSupport LocalHttpDatagramSupport();

  // Sends any data which should be sent at the start of a connection, including
  // the initial SETTINGS frame.  When using 0-RTT, this method is called twice:
  // once when encryption is established, and again when 1-RTT keys are
  // available.
  void SendInitialData();

  // Sends any data which should be sent after the initial SETTINGS frame.
  virtual void SendInitialDataAfterSettings() {}

  // Override to skip checking for qpack_decoder_send_stream_ given decoder data
  // is always bundled opportunistically.
  bool CheckStreamWriteBlocked(QuicStream* stream) const override;

  // Disables the use of Huffman encoding for QPACK headers.
  void DisableHuffmanEncoding() {
    huffman_encoding_ = HuffmanEncoding::kDisabled;
  }

  // Disables cookie crumbling for QPACK headers.
  void DisableCookieCrumbling() {
    cookie_crumbling_ = CookieCrumbling::kDisabled;
  }

  QuicSendControlStream* send_control_stream() { return send_control_stream_; }

 private:
  friend class test::QuicSpdySessionPeer;

  class SpdyFramerVisitor;

  // Proxies OnDatagramProcessed() calls to the session.
  class QUICHE_EXPORT DatagramObserver : public QuicDatagramQueue::Observer {
   public:
    explicit DatagramObserver(QuicSpdySession* session) : session_(session) {}
    void OnDatagramProcessed(std::optional<MessageStatus> status) override;

   private:
    QuicSpdySession* session_;  // not owned
  };

  struct QUICHE_EXPORT BufferedWebTransportStream {
    WebTransportSessionId session_id;
    QuicStreamId stream_id;
  };

  // The following methods are called by the SimpleVisitor.

  // Called when a HEADERS frame has been received.
  void OnHeaders(spdy::SpdyStreamId stream_id, bool has_priority,
                 const spdy::SpdyStreamPrecedence& precedence, bool fin);

  // Called when a PRIORITY frame has been received.
  void OnPriority(spdy::SpdyStreamId stream_id,
                  const spdy::SpdyStreamPrecedence& precedence);

  void CloseConnectionOnDuplicateHttp3UnidirectionalStreams(
      absl::string_view type);

  void FillSettingsFrame();

  bool VerifySettingIsZeroOrOne(uint64_t id, uint64_t value);

  // Computes the highest WebTransport version supported by both peers.
  std::optional<WebTransportHttp3Version> NegotiatedWebTransportVersion()
      const {
    return (LocallySupportedWebTransportVersions() &
            peer_web_transport_versions_)
        .Max();
  }

  bool ValidateWebTransportSettingsConsistency();

  HuffmanEncoding huffman_encoding_ = HuffmanEncoding::kEnabled;
  CookieCrumbling cookie_crumbling_ = CookieCrumbling::kEnabled;
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

  SettingsFrame settings_;

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
  size_t frame_len_;
  bool fin_;

  spdy::SpdyFramer spdy_framer_;
  http2::Http2DecoderAdapter h2_deframer_;
  std::unique_ptr<SpdyFramerVisitor> spdy_framer_visitor_;

  // Not owned by the session.
  Http3DebugVisitor* debug_visitor_;

  // Priority values received in PRIORITY_UPDATE frames for streams that are not
  // open yet.
  absl::flat_hash_map<QuicStreamId, HttpStreamPriority>
      buffered_stream_priorities_;

  // An integer used for live check. The indicator is assigned a value in
  // constructor. As long as it is not the assigned value, that would indicate
  // an use-after-free.
  int32_t destruction_indicator_;

  // The identifier in the most recently received GOAWAY frame.  Unset if no
  // GOAWAY frame has been received yet.
  std::optional<uint64_t> last_received_http3_goaway_id_;
  // The identifier in the most recently sent GOAWAY frame.  Unset if no GOAWAY
  // frame has been sent yet.
  std::optional<uint64_t> last_sent_http3_goaway_id_;

  // Whether both this endpoint and our peer support HTTP datagrams and which
  // draft is in use for this session.
  HttpDatagramSupport http_datagram_support_ = HttpDatagramSupport::kNone;

  // WebTransport protocol versions supported by the peer.
  WebTransportHttp3VersionSet peer_web_transport_versions_;

  // Whether the SETTINGS frame has been received on the control stream.
  bool settings_received_ = false;

  // If ShouldBufferRequestsUntilSettings() is true, all streams that are
  // blocked by that are tracked here.
  absl::flat_hash_set<QuicStreamId> streams_waiting_for_settings_;

  // WebTransport streams that do not have a session associated with them.
  // Limited to kMaxUnassociatedWebTransportStreams; when the list is full,
  // oldest streams are evicated first.
  std::list<BufferedWebTransportStream> buffered_streams_;

  // On the server side, if true, advertise and accept extended CONNECT method.
  // On the client side, true if the peer advertised extended CONNECT.
  bool allow_extended_connect_;

  // Since WebTransport is versioned by renumbering
  // SETTINGS_WEBTRANSPORT_MAX_SESSIONS, the max sessions value depends on the
  // version we end up picking.  This is only stored on the client, as the
  // server cannot initiate WebTransport sessions.
  absl::flat_hash_map<WebTransportHttp3Version, QuicStreamCount>
      max_webtransport_sessions_;

  // Allows forcing ShouldBufferRequestsUntilSettings() to true via
  // a connection option.
  bool force_buffer_requests_until_settings_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_SESSION_H_
