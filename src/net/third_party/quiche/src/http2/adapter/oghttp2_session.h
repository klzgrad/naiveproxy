#ifndef QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_
#define QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_

#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "http2/adapter/data_source.h"
#include "http2/adapter/event_forwarder.h"
#include "http2/adapter/header_validator.h"
#include "http2/adapter/http2_protocol.h"
#include "http2/adapter/http2_session.h"
#include "http2/adapter/http2_util.h"
#include "http2/adapter/http2_visitor_interface.h"
#include "http2/adapter/window_manager.h"
#include "http2/core/http2_trace_logging.h"
#include "http2/core/priority_write_scheduler.h"
#include "common/platform/api/quiche_bug_tracker.h"
#include "common/platform/api/quiche_export.h"
#include "spdy/core/http2_frame_decoder_adapter.h"
#include "spdy/core/no_op_headers_handler.h"
#include "spdy/core/spdy_framer.h"
#include "spdy/core/spdy_header_block.h"
#include "spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {

// This class manages state associated with a single multiplexed HTTP/2 session.
class QUICHE_EXPORT_PRIVATE OgHttp2Session
    : public Http2Session,
      public spdy::SpdyFramerVisitorInterface,
      public spdy::ExtensionVisitorInterface {
 public:
  struct QUICHE_EXPORT_PRIVATE Options {
    Perspective perspective = Perspective::kClient;
    // The maximum HPACK table size to use.
    absl::optional<size_t> max_hpack_encoding_table_capacity = absl::nullopt;
    // Whether to automatically send PING acks when receiving a PING.
    bool auto_ping_ack = true;
    // Whether (as server) to send a RST_STREAM NO_ERROR when sending a fin on
    // an incomplete stream.
    bool rst_stream_no_error_when_incomplete = false;
    // Whether (as server) to queue trailers until after a stream's data source
    // has indicated the end of data. If false, the server will assume that
    // submitting trailers indicates the end of data.
    bool trailers_require_end_data = false;
  };

  OgHttp2Session(Http2VisitorInterface& visitor, Options options);
  ~OgHttp2Session() override;

  // Enqueues a frame for transmission to the peer.
  void EnqueueFrame(std::unique_ptr<spdy::SpdyFrameIR> frame);

  // Starts a graceful shutdown sequence. No-op if a GOAWAY has already been
  // sent.
  void StartGracefulShutdown();

  // Invokes the visitor's OnReadyToSend() method for serialized frames and
  // DataFrameSource::Send() for data frames.
  int Send();

  int32_t SubmitRequest(absl::Span<const Header> headers,
                        std::unique_ptr<DataFrameSource> data_source,
                        void* user_data);
  int SubmitResponse(Http2StreamId stream_id, absl::Span<const Header> headers,
                     std::unique_ptr<DataFrameSource> data_source);
  int SubmitTrailer(Http2StreamId stream_id, absl::Span<const Header> trailers);
  void SubmitMetadata(Http2StreamId stream_id,
                      std::unique_ptr<MetadataSource> source);
  void SubmitSettings(absl::Span<const Http2Setting> settings);

  bool IsServerSession() const {
    return options_.perspective == Perspective::kServer;
  }
  Http2StreamId GetHighestReceivedStreamId() const {
    return highest_received_stream_id_;
  }
  void SetStreamUserData(Http2StreamId stream_id, void* user_data);
  void* GetStreamUserData(Http2StreamId stream_id);

  // Resumes a stream that was previously blocked. Returns true on success.
  bool ResumeStream(Http2StreamId stream_id);

  // Returns the peer's outstanding stream receive window for the given stream.
  int GetStreamSendWindowSize(Http2StreamId stream_id) const;

  // Returns the current upper bound on the flow control receive window for this
  // stream.
  int GetStreamReceiveWindowLimit(Http2StreamId stream_id) const;

  // Returns the outstanding stream receive window, or -1 if the stream does not
  // exist.
  int GetStreamReceiveWindowSize(Http2StreamId stream_id) const;

  // Returns the outstanding connection receive window.
  int GetReceiveWindowSize() const;

  // Returns the size of the HPACK encoder's dynamic table, including the
  // per-entry overhead from the specification.
  int GetHpackEncoderDynamicTableSize() const;

  // Returns the maximum capacity of the HPACK encoder's dynamic table.
  int GetHpackEncoderDynamicTableCapacity() const;

  // Returns the size of the HPACK decoder's dynamic table, including the
  // per-entry overhead from the specification.
  int GetHpackDecoderDynamicTableSize() const;

  // Returns the size of the HPACK decoder's most recently applied size limit.
  int GetHpackDecoderSizeLimit() const;

  // From Http2Session.
  int64_t ProcessBytes(absl::string_view bytes) override;
  int Consume(Http2StreamId stream_id, size_t num_bytes) override;
  bool want_read() const override {
    return !received_goaway_ && !decoder_.HasError();
  }
  bool want_write() const override {
    return !frames_.empty() || !buffered_data_.empty() ||
           write_scheduler_.HasReadyStreams() || !connection_metadata_.empty();
  }
  int GetRemoteWindowSize() const override { return connection_send_window_; }

  // From SpdyFramerVisitorInterface
  void OnError(http2::Http2DecoderAdapter::SpdyFramerError error,
               std::string detailed_error) override;
  void OnCommonHeader(spdy::SpdyStreamId /*stream_id*/,
                      size_t /*length*/,
                      uint8_t /*type*/,
                      uint8_t /*flags*/) override;
  void OnDataFrameHeader(spdy::SpdyStreamId stream_id,
                         size_t length,
                         bool fin) override;
  void OnStreamFrameData(spdy::SpdyStreamId stream_id,
                         const char* data,
                         size_t len) override;
  void OnStreamEnd(spdy::SpdyStreamId stream_id) override;
  void OnStreamPadLength(spdy::SpdyStreamId /*stream_id*/,
                         size_t /*value*/) override;
  void OnStreamPadding(spdy::SpdyStreamId stream_id, size_t len) override;
  spdy::SpdyHeadersHandlerInterface* OnHeaderFrameStart(
      spdy::SpdyStreamId stream_id) override;
  void OnHeaderFrameEnd(spdy::SpdyStreamId stream_id) override;
  void OnRstStream(spdy::SpdyStreamId stream_id,
                   spdy::SpdyErrorCode error_code) override;
  void OnSettings() override;
  void OnSetting(spdy::SpdySettingsId id, uint32_t value) override;
  void OnSettingsEnd() override;
  void OnSettingsAck() override;
  void OnPing(spdy::SpdyPingId unique_id, bool is_ack) override;
  void OnGoAway(spdy::SpdyStreamId last_accepted_stream_id,
                spdy::SpdyErrorCode error_code) override;
  bool OnGoAwayFrameData(const char* goaway_data, size_t len) override;
  void OnHeaders(spdy::SpdyStreamId stream_id,
                 bool has_priority,
                 int weight,
                 spdy::SpdyStreamId parent_stream_id,
                 bool exclusive,
                 bool fin,
                 bool end) override;
  void OnWindowUpdate(spdy::SpdyStreamId stream_id,
                      int delta_window_size) override;
  void OnPushPromise(spdy::SpdyStreamId stream_id,
                     spdy::SpdyStreamId promised_stream_id,
                     bool end) override;
  void OnContinuation(spdy::SpdyStreamId stream_id, bool end) override;
  void OnAltSvc(spdy::SpdyStreamId /*stream_id*/, absl::string_view /*origin*/,
                const spdy::SpdyAltSvcWireFormat::
                    AlternativeServiceVector& /*altsvc_vector*/) override;
  void OnPriority(spdy::SpdyStreamId stream_id,
                  spdy::SpdyStreamId parent_stream_id,
                  int weight,
                  bool exclusive) override;
  void OnPriorityUpdate(spdy::SpdyStreamId prioritized_stream_id,
                        absl::string_view priority_field_value) override;
  bool OnUnknownFrame(spdy::SpdyStreamId stream_id,
                      uint8_t frame_type) override;

  // Invoked when header processing encounters an invalid or otherwise
  // problematic header.
  void OnHeaderStatus(Http2StreamId stream_id,
                      Http2VisitorInterface::OnHeaderResult result);

  // Returns true if a recognized extension frame is received.
  bool OnFrameHeader(spdy::SpdyStreamId stream_id, size_t length, uint8_t type,
                     uint8_t flags) override;

  // Handles the payload for a recognized extension frame.
  void OnFramePayload(const char* data, size_t len) override;

 private:
  using MetadataSequence = std::vector<std::unique_ptr<MetadataSource>>;

  struct QUICHE_EXPORT_PRIVATE StreamState {
    StreamState(int32_t stream_receive_window,
                WindowManager::WindowUpdateListener listener)
        : window_manager(stream_receive_window, std::move(listener)) {}

    WindowManager window_manager;
    std::unique_ptr<DataFrameSource> outbound_body;
    MetadataSequence outbound_metadata;
    std::unique_ptr<spdy::SpdyHeaderBlock> trailers;
    void* user_data = nullptr;
    int32_t send_window = kInitialFlowControlWindowSize;
    absl::optional<HeaderType> received_header_type;
    bool half_closed_local = false;
    bool half_closed_remote = false;
    // Indicates that `outbound_body` temporarily cannot produce data.
    bool data_deferred = false;
  };
  using StreamStateMap = absl::flat_hash_map<Http2StreamId, StreamState>;

  struct QUICHE_EXPORT_PRIVATE PendingStreamState {
    Http2StreamId stream_id;
    spdy::SpdyHeaderBlock headers;
    std::unique_ptr<DataFrameSource> data_source;
    void* user_data = nullptr;
  };

  class QUICHE_EXPORT_PRIVATE PassthroughHeadersHandler
      : public spdy::SpdyHeadersHandlerInterface {
   public:
    explicit PassthroughHeadersHandler(OgHttp2Session& session,
                                       Http2VisitorInterface& visitor)
        : session_(session), visitor_(visitor) {}

    void set_stream_id(Http2StreamId stream_id) {
      stream_id_ = stream_id;
      result_ = Http2VisitorInterface::HEADER_OK;
    }

    void set_frame_contains_fin() { frame_contains_fin_ = true; }
    void set_header_type(HeaderType type) { type_ = type; }
    HeaderType header_type() const { return type_; }

    void OnHeaderBlockStart() override;
    void OnHeader(absl::string_view key, absl::string_view value) override;
    void OnHeaderBlockEnd(size_t /* uncompressed_header_bytes */,
                          size_t /* compressed_header_bytes */) override;
    absl::string_view status_header() {
      QUICHE_DCHECK(type_ == HeaderType::RESPONSE ||
                    type_ == HeaderType::RESPONSE_100);
      return validator_.status_header();
    }

   private:
    OgHttp2Session& session_;
    Http2VisitorInterface& visitor_;
    Http2StreamId stream_id_ = 0;
    Http2VisitorInterface::OnHeaderResult result_ =
        Http2VisitorInterface::HEADER_OK;
    // Validates header blocks according to the HTTP/2 specification.
    HeaderValidator validator_;
    HeaderType type_ = HeaderType::RESPONSE;
    bool frame_contains_fin_ = false;
  };

  // Queues the connection preface, if not already done.
  void MaybeSetupPreface();

  // Gets the settings to be sent in the initial SETTINGS frame sent as part of
  // the connection preface.
  std::vector<Http2Setting> GetInitialSettings() const;

  // Prepares and returns a SETTINGS frame with the given `settings`.
  std::unique_ptr<spdy::SpdySettingsIR> PrepareSettingsFrame(
      absl::Span<const Http2Setting> settings);

  void SendWindowUpdate(Http2StreamId stream_id, size_t update_delta);

  enum class SendResult {
    // All data was flushed.
    SEND_OK,
    // Not all data was flushed (due to flow control or TCP back pressure).
    SEND_BLOCKED,
    // An error occurred while sending data.
    SEND_ERROR,
  };

  // Sends the buffered connection preface or serialized frame data, if any.
  SendResult MaybeSendBufferedData();

  // Serializes and sends queued frames.
  SendResult SendQueuedFrames();

  void AfterFrameSent(uint8_t frame_type, uint32_t stream_id,
                      size_t payload_length, uint8_t flags,
                      uint32_t error_code);

  // Writes DATA frames for stream `stream_id`.
  SendResult WriteForStream(Http2StreamId stream_id);

  SendResult SendMetadata(Http2StreamId stream_id, MetadataSequence& sequence);

  void SendHeaders(Http2StreamId stream_id, spdy::SpdyHeaderBlock headers,
                   bool end_stream);

  void SendTrailers(Http2StreamId stream_id, spdy::SpdyHeaderBlock trailers);

  // Encapsulates the RST_STREAM NO_ERROR behavior described in RFC 7540
  // Section 8.1.
  void MaybeFinWithRstStream(StreamStateMap::iterator iter);

  // Performs flow control accounting for data sent by the peer.
  void MarkDataBuffered(Http2StreamId stream_id, size_t bytes);

  // Creates a stream for `stream_id` if not already present and returns an
  // iterator pointing to it.
  StreamStateMap::iterator CreateStream(Http2StreamId stream_id);

  // Creates a stream for `stream_id`, stores the `data_source` and `user_data`
  // in the stream state, and sends the `headers`.
  void StartRequest(Http2StreamId stream_id, spdy::SpdyHeaderBlock headers,
                    std::unique_ptr<DataFrameSource> data_source,
                    void* user_data);

  // Closes the given `stream_id` with the given `error_code`.
  void CloseStream(Http2StreamId stream_id, Http2ErrorCode error_code);

  // Calculates the next expected header type for a stream in a given state.
  HeaderType NextHeaderType(absl::optional<HeaderType> current_type);

  // Returns true if the session can create a new stream.
  bool CanCreateStream() const;

  // Informs the visitor of the connection `error` and stops processing on the
  // connection. If server-side, also sends a GOAWAY with `error_code`.
  void LatchErrorAndNotify(Http2ErrorCode error_code,
                           Http2VisitorInterface::ConnectionError error);

  void CloseStreamIfReady(uint8_t frame_type, uint32_t stream_id);

  // Receives events when inbound frames are parsed.
  Http2VisitorInterface& visitor_;

  // Forwards received events to the session if it can accept them.
  EventForwarder event_forwarder_;

  // Logs received frames when enabled.
  Http2TraceLogger receive_logger_;
  // Logs sent frames when enabled.
  Http2FrameLogger send_logger_;

  // Encodes outbound frames.
  spdy::SpdyFramer framer_{spdy::SpdyFramer::ENABLE_COMPRESSION};

  // Decodes inbound frames.
  http2::Http2DecoderAdapter decoder_;

  // Maintains the state of active streams known to this session.
  StreamStateMap stream_map_;

  // Maintains the state of pending streams known to this session. A pending
  // stream is kept in this list until it can be created while complying with
  // `max_outbound_concurrent_streams_`.
  std::list<PendingStreamState> pending_streams_;

  // The queue of outbound frames.
  std::list<std::unique_ptr<spdy::SpdyFrameIR>> frames_;
  // Buffered data (connection preface, serialized frames) that has not yet been
  // sent.
  std::string buffered_data_;

  // Maintains the set of streams ready to write data to the peer.
  using WriteScheduler = PriorityWriteScheduler<Http2StreamId>;
  WriteScheduler write_scheduler_;

  // Stores the queue of callbacks to invoke upon receiving SETTINGS acks. At
  // most one callback is invoked for each SETTINGS ack.
  using SettingsAckCallback = std::function<void()>;
  std::list<SettingsAckCallback> settings_ack_callbacks_;

  // Delivers header name-value pairs to the visitor.
  PassthroughHeadersHandler headers_handler_;

  // Ignores header data, e.g., for an unknown or rejected stream.
  spdy::NoOpHeadersHandler noop_headers_handler_;

  // Tracks the remaining client connection preface, in the case of a server
  // session.
  absl::string_view remaining_preface_;

  WindowManager connection_window_manager_;

  absl::flat_hash_set<Http2StreamId> streams_reset_;
  absl::flat_hash_map<Http2StreamId, int> queued_frames_;

  MetadataSequence connection_metadata_;

  Http2StreamId next_stream_id_ = 1;
  // The highest received stream ID is the highest stream ID in any frame read
  // from the peer. The highest processed stream ID is the highest stream ID for
  // which this endpoint created a stream in the stream map.
  Http2StreamId highest_received_stream_id_ = 0;
  Http2StreamId highest_processed_stream_id_ = 0;
  Http2StreamId metadata_stream_id_ = 0;
  size_t metadata_length_ = 0;
  int32_t connection_send_window_ = kInitialFlowControlWindowSize;
  // The initial flow control receive window size for any newly created streams.
  int32_t stream_receive_window_limit_ = kInitialFlowControlWindowSize;
  uint32_t max_frame_payload_ = 16384u;
  // The maximum number of concurrent streams that this connection can open to
  // its peer and allow from its peer, respectively. Although the initial value
  // is unlimited, the spec encourages a value of at least 100. We limit
  // ourselves to opening 100 until told otherwise by the peer and allow an
  // unlimited number from the peer until updated from SETTINGS we send.
  uint32_t max_outbound_concurrent_streams_ = 100u;
  uint32_t pending_max_inbound_concurrent_streams_ =
      std::numeric_limits<uint32_t>::max();
  uint32_t max_inbound_concurrent_streams_ =
      std::numeric_limits<uint32_t>::max();
  Options options_;

  // The HPACK encoder header table capacity that will be applied when
  // acking SETTINGS from the peer. Only contains a value if the peer advertises
  // a larger table capacity than currently used; a smaller value can safely be
  // applied immediately upon receipt.
  absl::optional<uint32_t> encoder_header_table_capacity_when_acking_;

  bool received_goaway_ = false;
  bool queued_preface_ = false;
  bool peer_supports_metadata_ = false;
  bool end_metadata_ = false;

  // Recursion guard for ProcessBytes().
  bool processing_bytes_ = false;
  // Recursion guard for Send().
  bool sending_ = false;

  // Replace this with a stream ID, for multiple GOAWAY support.
  bool queued_goaway_ = false;
  bool latched_error_ = false;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_
