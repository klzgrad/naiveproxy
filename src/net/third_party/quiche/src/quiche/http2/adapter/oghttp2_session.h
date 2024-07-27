#ifndef QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_
#define QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_

#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "quiche/http2/adapter/chunked_buffer.h"
#include "quiche/http2/adapter/data_source.h"
#include "quiche/http2/adapter/event_forwarder.h"
#include "quiche/http2/adapter/header_validator.h"
#include "quiche/http2/adapter/header_validator_base.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/http2_session.h"
#include "quiche/http2/adapter/http2_util.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/window_manager.h"
#include "quiche/http2/core/http2_trace_logging.h"
#include "quiche/http2/core/priority_write_scheduler.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_flags.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_linked_hash_map.h"
#include "quiche/spdy/core/http2_frame_decoder_adapter.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/no_op_headers_handler.h"
#include "quiche/spdy/core/spdy_framer.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {

// This class manages state associated with a single multiplexed HTTP/2 session.
class QUICHE_EXPORT OgHttp2Session : public Http2Session,
                                     public spdy::SpdyFramerVisitorInterface {
 public:
  struct QUICHE_EXPORT Options {
    // Returns whether to send a WINDOW_UPDATE based on the window limit, window
    // size, and delta that would be sent in the WINDOW_UPDATE.
    WindowManager::ShouldWindowUpdateFn should_window_update_fn =
        DeltaAtLeastHalfLimit;
    // The perspective of this session.
    Perspective perspective = Perspective::kClient;
    // The maximum HPACK table size to use.
    std::optional<size_t> max_hpack_encoding_table_capacity;
    // The maximum number of decoded header bytes that a stream can receive.
    std::optional<uint32_t> max_header_list_bytes = std::nullopt;
    // The maximum size of an individual header field, including name and value.
    std::optional<uint32_t> max_header_field_size = std::nullopt;
    // The assumed initial value of the remote endpoint's max concurrent streams
    // setting.
    std::optional<uint32_t> remote_max_concurrent_streams = std::nullopt;
    // Whether to automatically send PING acks when receiving a PING.
    bool auto_ping_ack = true;
    // Whether (as server) to send a RST_STREAM NO_ERROR when sending a fin on
    // an incomplete stream.
    bool rst_stream_no_error_when_incomplete = false;
    // Whether to mark all input data as consumed upon encountering a connection
    // error while processing bytes. If true, subsequent processing will also
    // mark all input data as consumed.
    bool blackhole_data_on_connection_error = true;
    // Whether to advertise support for the extended CONNECT semantics described
    // in RFC 8441. If true, this endpoint will send the appropriate setting in
    // initial SETTINGS.
    bool allow_extended_connect = true;
    // Whether to allow `obs-text` (characters from hexadecimal 0x80 to 0xff) in
    // header field values.
    bool allow_obs_text = true;
    // If true, validates header field names and values according to RFC 7230
    // and RFC 7540.
    bool validate_http_headers = true;
    // If true, validate the `:path` pseudo-header according to RFC 3986
    // Section 3.3.
    bool validate_path = false;
    // If true, allows the '#' character in request paths, even though this
    // contradicts RFC 3986 Section 3.3.
    // TODO(birenroy): Flip the default value to false.
    bool allow_fragment_in_path = true;
    // If true, allows different values for `host` and `:authority` headers to
    // be present in request headers.
    bool allow_different_host_and_authority = false;
    // If true, crumbles `Cookie` header field values for potentially better
    // HPACK compression.
    bool crumble_cookies = false;
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
                        bool end_stream, void* user_data);
  int SubmitResponse(Http2StreamId stream_id, absl::Span<const Header> headers,
                     std::unique_ptr<DataFrameSource> data_source,
                     bool end_stream);
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

  uint32_t GetMaxOutboundConcurrentStreams() const {
    return max_outbound_concurrent_streams_;
  }

  // From Http2Session.
  int64_t ProcessBytes(absl::string_view bytes) override;
  int Consume(Http2StreamId stream_id, size_t num_bytes) override;
  bool want_read() const override {
    return !received_goaway_ && !decoder_.HasError();
  }
  bool want_write() const override {
    return !fatal_send_error_ &&
           (!frames_.empty() || !buffered_data_.Empty() || HasReadyStream() ||
            !goaway_rejected_streams_.empty());
  }
  int GetRemoteWindowSize() const override { return connection_send_window_; }
  bool peer_enables_connect_protocol() {
    return peer_enables_connect_protocol_;
  }

  // From SpdyFramerVisitorInterface
  void OnError(http2::Http2DecoderAdapter::SpdyFramerError error,
               std::string detailed_error) override;
  void OnCommonHeader(spdy::SpdyStreamId /*stream_id*/, size_t /*length*/,
                      uint8_t /*type*/, uint8_t /*flags*/) override;
  void OnDataFrameHeader(spdy::SpdyStreamId stream_id, size_t length,
                         bool fin) override;
  void OnStreamFrameData(spdy::SpdyStreamId stream_id, const char* data,
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
  void OnHeaders(spdy::SpdyStreamId stream_id, size_t payload_length,
                 bool has_priority, int weight,
                 spdy::SpdyStreamId parent_stream_id, bool exclusive, bool fin,
                 bool end) override;
  void OnWindowUpdate(spdy::SpdyStreamId stream_id,
                      int delta_window_size) override;
  void OnPushPromise(spdy::SpdyStreamId stream_id,
                     spdy::SpdyStreamId promised_stream_id, bool end) override;
  void OnContinuation(spdy::SpdyStreamId stream_id, size_t payload_length,
                      bool end) override;
  void OnAltSvc(spdy::SpdyStreamId /*stream_id*/, absl::string_view /*origin*/,
                const spdy::SpdyAltSvcWireFormat::
                    AlternativeServiceVector& /*altsvc_vector*/) override;
  void OnPriority(spdy::SpdyStreamId stream_id,
                  spdy::SpdyStreamId parent_stream_id, int weight,
                  bool exclusive) override;
  void OnPriorityUpdate(spdy::SpdyStreamId prioritized_stream_id,
                        absl::string_view priority_field_value) override;
  bool OnUnknownFrame(spdy::SpdyStreamId stream_id,
                      uint8_t frame_type) override;
  void OnUnknownFrameStart(spdy::SpdyStreamId stream_id, size_t length,
                           uint8_t type, uint8_t flags) override;
  void OnUnknownFramePayload(spdy::SpdyStreamId stream_id,
                             absl::string_view payload) override;

  // Invoked when header processing encounters an invalid or otherwise
  // problematic header.
  void OnHeaderStatus(Http2StreamId stream_id,
                      Http2VisitorInterface::OnHeaderResult result);

 private:
  struct QUICHE_EXPORT StreamState {
    StreamState(int32_t stream_receive_window, int32_t stream_send_window,
                WindowManager::WindowUpdateListener listener,
                WindowManager::ShouldWindowUpdateFn should_window_update_fn)
        : window_manager(stream_receive_window, std::move(listener),
                         std::move(should_window_update_fn),
                         /*update_window_on_notify=*/false),
          send_window(stream_send_window) {}

    WindowManager window_manager;
    std::unique_ptr<DataFrameSource> outbound_body;
    std::unique_ptr<spdy::Http2HeaderBlock> trailers;
    void* user_data = nullptr;
    int32_t send_window;
    std::optional<HeaderType> received_header_type;
    std::optional<size_t> remaining_content_length;
    bool check_visitor_for_body = false;
    bool half_closed_local = false;
    bool half_closed_remote = false;
    // Indicates that `outbound_body` temporarily cannot produce data.
    bool data_deferred = false;
    bool sent_head_method = false;
    bool can_receive_body = true;
  };
  using StreamStateMap = absl::flat_hash_map<Http2StreamId, StreamState>;

  struct QUICHE_EXPORT PendingStreamState {
    spdy::Http2HeaderBlock headers;
    std::unique_ptr<DataFrameSource> data_source;
    void* user_data = nullptr;
    bool end_stream;
  };

  class QUICHE_EXPORT PassthroughHeadersHandler
      : public spdy::SpdyHeadersHandlerInterface {
   public:
    PassthroughHeadersHandler(OgHttp2Session& session,
                              Http2VisitorInterface& visitor);

    void Reset() {
      error_encountered_ = false;
    }

    void set_stream_id(Http2StreamId stream_id) { stream_id_ = stream_id; }
    void set_frame_contains_fin(bool value) { frame_contains_fin_ = value; }
    void set_header_type(HeaderType type) { type_ = type; }
    HeaderType header_type() const { return type_; }

    void OnHeaderBlockStart() override;
    void OnHeader(absl::string_view key, absl::string_view value) override;
    void OnHeaderBlockEnd(size_t /* uncompressed_header_bytes */,
                          size_t /* compressed_header_bytes */) override;
    absl::string_view status_header() const {
      QUICHE_DCHECK(type_ == HeaderType::RESPONSE ||
                    type_ == HeaderType::RESPONSE_100);
      return validator_->status_header();
    }
    std::optional<size_t> content_length() const {
      return validator_->content_length();
    }
    void SetAllowExtendedConnect() { validator_->SetAllowExtendedConnect(); }
    void SetMaxFieldSize(uint32_t field_size) {
      validator_->SetMaxFieldSize(field_size);
    }
    void SetAllowObsText(bool allow) {
      validator_->SetObsTextOption(allow ? ObsTextOption::kAllow
                                         : ObsTextOption::kDisallow);
    }
    bool CanReceiveBody() const;

   private:
    void SetResult(Http2VisitorInterface::OnHeaderResult result);

    OgHttp2Session& session_;
    Http2VisitorInterface& visitor_;
    Http2StreamId stream_id_ = 0;
    // Validates header blocks according to the HTTP/2 specification.
    std::unique_ptr<HeaderValidatorBase> validator_;
    HeaderType type_ = HeaderType::RESPONSE;
    bool frame_contains_fin_ = false;
    bool error_encountered_ = false;
  };

  struct QUICHE_EXPORT ProcessBytesResultVisitor;

  // Queues the connection preface, if not already done. If not
  // `sending_outbound_settings` and the preface has not yet been queued, this
  // method will generate and enqueue initial SETTINGS.
  void MaybeSetupPreface(bool sending_outbound_settings);

  // Gets the settings to be sent in the initial SETTINGS frame sent as part of
  // the connection preface.
  std::vector<Http2Setting> GetInitialSettings() const;

  // Prepares and returns a SETTINGS frame with the given `settings`.
  std::unique_ptr<spdy::SpdySettingsIR> PrepareSettingsFrame(
      absl::Span<const Http2Setting> settings);

  // Updates internal state to match the SETTINGS advertised to the peer.
  void HandleOutboundSettings(const spdy::SpdySettingsIR& settings_frame);

  void SendWindowUpdate(Http2StreamId stream_id, size_t update_delta);

  enum class SendResult {
    // All data was flushed.
    SEND_OK,
    // Not all data was flushed (due to flow control or TCP back pressure).
    SEND_BLOCKED,
    // An error occurred while sending data.
    SEND_ERROR,
  };

  // Returns the int corresponding to the `result`, updating state as needed.
  int InterpretSendResult(SendResult result);

  enum class ProcessBytesError {
    // A general, unspecified error.
    kUnspecified,
    // The (server-side) session received an invalid client connection preface.
    kInvalidConnectionPreface,
    // A user/visitor callback failed with a fatal error.
    kVisitorCallbackFailed,
  };
  using ProcessBytesResult = absl::variant<int64_t, ProcessBytesError>;

  // Attempts to process `bytes` and returns the number of bytes proccessed on
  // success or the processing error on failure.
  ProcessBytesResult ProcessBytesImpl(absl::string_view bytes);

  // Returns true if at least one stream has data or control frames to write.
  bool HasReadyStream() const;

  // Returns the next stream that has something to write. If there are no such
  // streams, returns zero.
  Http2StreamId GetNextReadyStream();

  int32_t SubmitRequestInternal(absl::Span<const Header> headers,
                                std::unique_ptr<DataFrameSource> data_source,
                                bool end_stream, void* user_data);
  int SubmitResponseInternal(Http2StreamId stream_id,
                             absl::Span<const Header> headers,
                             std::unique_ptr<DataFrameSource> data_source,
                             bool end_stream);

  // Sends the buffered connection preface or serialized frame data, if any.
  SendResult MaybeSendBufferedData();

  // Serializes and sends queued frames.
  SendResult SendQueuedFrames();

  // Returns false if a fatal connection error occurred.
  bool AfterFrameSent(uint8_t frame_type_int, uint32_t stream_id,
                      size_t payload_length, uint8_t flags,
                      uint32_t error_code);

  // Writes DATA frames for stream `stream_id`.
  SendResult WriteForStream(Http2StreamId stream_id);

  void SerializeMetadata(Http2StreamId stream_id,
                         std::unique_ptr<MetadataSource> source);

  void SendHeaders(Http2StreamId stream_id, spdy::Http2HeaderBlock headers,
                   bool end_stream);

  void SendTrailers(Http2StreamId stream_id, spdy::Http2HeaderBlock trailers);

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
  void StartRequest(Http2StreamId stream_id, spdy::Http2HeaderBlock headers,
                    std::unique_ptr<DataFrameSource> data_source,
                    void* user_data, bool end_stream);

  // Sends headers for pending streams as long as the stream limit allows.
  void StartPendingStreams();

  // Closes the given `stream_id` with the given `error_code`.
  void CloseStream(Http2StreamId stream_id, Http2ErrorCode error_code);

  // Calculates the next expected header type for a stream in a given state.
  HeaderType NextHeaderType(std::optional<HeaderType> current_type);

  // Returns true if the session can create a new stream.
  bool CanCreateStream() const;

  // Informs the visitor of the connection `error` and stops processing on the
  // connection. If server-side, also sends a GOAWAY with `error_code`.
  void LatchErrorAndNotify(Http2ErrorCode error_code,
                           Http2VisitorInterface::ConnectionError error);

  void CloseStreamIfReady(uint8_t frame_type, uint32_t stream_id);

  // Informs the visitor of rejected, non-active streams due to GOAWAY receipt.
  void CloseGoAwayRejectedStreams();

  // Updates internal state to prepare for sending an immediate GOAWAY.
  void PrepareForImmediateGoAway();

  // Handles the potential end of received metadata for the given `stream_id`.
  void MaybeHandleMetadataEndForStream(Http2StreamId stream_id);

  void DecrementQueuedFrameCount(uint32_t stream_id, uint8_t frame_type);

  void HandleContentLengthError(Http2StreamId stream_id);

  // Invoked when sending a flow control window update to the peer.
  void UpdateReceiveWindow(Http2StreamId stream_id, int32_t delta);

  // Updates stream send window accounting to respect the peer's advertised
  // initial window setting.
  void UpdateStreamSendWindowSizes(uint32_t new_value);

  // Updates stream receive window managers to use the newly advertised stream
  // initial window.
  void UpdateStreamReceiveWindowSizes(uint32_t new_value);

  // Returns true if the given stream has additional data to write before
  // trailers or the end of the stream.
  bool HasMoreData(const StreamState& stream_state) const;

  // Returns true if the given stream has data ready to write. Trailers are
  // considered separately.
  bool IsReadyToWriteData(const StreamState& stream_state) const;

  // Abandons any remaining data, e.g. on stream reset.
  void AbandonData(StreamState& stream_state);

  // Gathers information required to construct a DATA frame header.
  using DataFrameHeaderInfo = Http2VisitorInterface::DataFrameHeaderInfo;
  DataFrameHeaderInfo GetDataFrameInfo(Http2StreamId stream_id,
                                       size_t flow_control_available,
                                       StreamState& stream_state);

  // Invokes the appropriate API to send a DATA frame header and payload.
  bool SendDataFrame(Http2StreamId stream_id, absl::string_view frame_header,
                     size_t payload_length, StreamState& stream_state);

  // Receives events when inbound frames are parsed.
  Http2VisitorInterface& visitor_;

  const Options options_;

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
  quiche::QuicheLinkedHashMap<Http2StreamId, PendingStreamState>
      pending_streams_;

  // The queue of outbound frames.
  std::list<std::unique_ptr<spdy::SpdyFrameIR>> frames_;
  // Buffered data (connection preface, serialized frames) that has not yet been
  // sent.
  ChunkedBuffer buffered_data_;

  // Maintains the set of streams ready to write data to the peer.
  using WriteScheduler = PriorityWriteScheduler<Http2StreamId>;
  WriteScheduler write_scheduler_;

  // Stores the queue of callbacks to invoke upon receiving SETTINGS acks. At
  // most one callback is invoked for each SETTINGS ack.
  using SettingsAckCallback = quiche::SingleUseCallback<void()>;
  quiche::QuicheCircularDeque<SettingsAckCallback> settings_ack_callbacks_;

  // Delivers header name-value pairs to the visitor.
  PassthroughHeadersHandler headers_handler_;

  // Ignores header data, e.g., for an unknown or rejected stream.
  spdy::NoOpHeadersHandler noop_headers_handler_;

  // Tracks the remaining client connection preface, in the case of a server
  // session.
  absl::string_view remaining_preface_;

  WindowManager connection_window_manager_;

  // Tracks the streams that have been marked for reset. A stream is removed
  // from this set once it is closed.
  absl::flat_hash_set<Http2StreamId> streams_reset_;

  // The number of frames currently queued per stream.
  absl::flat_hash_map<Http2StreamId, int> queued_frames_;
  // Includes streams that are currently ready to write trailers.
  absl::flat_hash_set<Http2StreamId> trailers_ready_;
  // Includes streams that will not be written due to receipt of GOAWAY.
  absl::flat_hash_set<Http2StreamId> goaway_rejected_streams_;

  Http2StreamId next_stream_id_ = 1;
  // The highest received stream ID is the highest stream ID in any frame read
  // from the peer. The highest processed stream ID is the highest stream ID for
  // which this endpoint created a stream in the stream map.
  Http2StreamId highest_received_stream_id_ = 0;
  Http2StreamId highest_processed_stream_id_ = 0;
  Http2StreamId received_goaway_stream_id_ = 0;
  size_t metadata_length_ = 0;
  int32_t connection_send_window_ = kInitialFlowControlWindowSize;
  // The initial flow control receive window size for any newly created streams.
  int32_t initial_stream_receive_window_ = kInitialFlowControlWindowSize;
  // The initial flow control send window size for any newly created streams.
  int32_t initial_stream_send_window_ = kInitialFlowControlWindowSize;
  uint32_t max_frame_payload_ = kDefaultFramePayloadSizeLimit;
  // The maximum number of concurrent streams that this connection can open to
  // its peer. Although the initial value
  // is unlimited, the spec encourages a value of at least 100. Initially 100 or
  // the specified option until told otherwise by the peer.
  uint32_t max_outbound_concurrent_streams_;
  // The maximum number of concurrent streams that this connection allows from
  // its peer. Unlimited, until SETTINGS with some other value is acknowledged.
  uint32_t pending_max_inbound_concurrent_streams_ =
      std::numeric_limits<uint32_t>::max();
  uint32_t max_inbound_concurrent_streams_ =
      std::numeric_limits<uint32_t>::max();

  // The HPACK encoder header table capacity that will be applied when
  // acking SETTINGS from the peer. Only contains a value if the peer advertises
  // a larger table capacity than currently used; a smaller value can safely be
  // applied immediately upon receipt.
  std::optional<uint32_t> encoder_header_table_capacity_when_acking_;

  uint8_t current_frame_type_ = 0;

  bool received_goaway_ = false;
  bool queued_preface_ = false;
  bool peer_supports_metadata_ = false;
  bool end_metadata_ = false;
  bool process_metadata_ = false;
  bool sent_non_ack_settings_ = false;

  // Recursion guard for ProcessBytes().
  bool processing_bytes_ = false;
  // Recursion guard for Send().
  bool sending_ = false;

  bool peer_enables_connect_protocol_ = false;

  // Replace this with a stream ID, for multiple GOAWAY support.
  bool queued_goaway_ = false;
  bool queued_immediate_goaway_ = false;
  bool latched_error_ = false;

  // True if a fatal sending error has occurred.
  bool fatal_send_error_ = false;

  // True if a fatal processing visitor callback failed.
  bool fatal_visitor_callback_failure_ = false;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_
