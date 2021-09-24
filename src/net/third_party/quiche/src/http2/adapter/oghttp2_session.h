#ifndef QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_
#define QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_

#include <list>

#include "http2/adapter/data_source.h"
#include "http2/adapter/http2_session.h"
#include "http2/adapter/http2_util.h"
#include "http2/adapter/http2_visitor_interface.h"
#include "http2/adapter/window_manager.h"
#include "http2/core/priority_write_scheduler.h"
#include "common/platform/api/quiche_bug_tracker.h"
#include "common/platform/api/quiche_export.h"
#include "spdy/core/http2_frame_decoder_adapter.h"
#include "spdy/core/spdy_framer.h"

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
  };

  OgHttp2Session(Http2VisitorInterface& visitor, Options options);
  ~OgHttp2Session() override;

  // Enqueues a frame for transmission to the peer.
  void EnqueueFrame(std::unique_ptr<spdy::SpdyFrameIR> frame);

  // Starts a graceful shutdown sequence. No-op if a GOAWAY has already been
  // sent.
  void StartGracefulShutdown();

  // Invokes the visitor's OnReadyToSend() method for serialized frame data.
  int Send();

  int32_t SubmitRequest(absl::Span<const Header> headers,
                        std::unique_ptr<DataFrameSource> data_source,
                        void* user_data);
  int SubmitResponse(Http2StreamId stream_id, absl::Span<const Header> headers,
                     std::unique_ptr<DataFrameSource> data_source);
  int SubmitTrailer(Http2StreamId stream_id, absl::Span<const Header> trailers);
  void SubmitMetadata(Http2StreamId stream_id,
                      std::unique_ptr<MetadataSource> source);

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

  // Returns the size of the HPACK decoder's dynamic table, including the
  // per-entry overhead from the specification.
  int GetHpackDecoderDynamicTableSize() const;

  // From Http2Session.
  ssize_t ProcessBytes(absl::string_view bytes) override;
  int Consume(Http2StreamId stream_id, size_t num_bytes) override;
  bool want_read() const override { return !received_goaway_; }
  bool want_write() const override {
    return !frames_.empty() || !serialized_prefix_.empty() ||
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
    bool half_closed_local = false;
    bool half_closed_remote = false;
  };
  using StreamStateMap = absl::flat_hash_map<Http2StreamId, StreamState>;

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
    void OnHeaderBlockStart() override;
    void OnHeader(absl::string_view key, absl::string_view value) override;
    void OnHeaderBlockEnd(size_t /* uncompressed_header_bytes */,
                          size_t /* compressed_header_bytes */) override;

   private:
    OgHttp2Session& session_;
    Http2VisitorInterface& visitor_;
    Http2StreamId stream_id_ = 0;
    Http2VisitorInterface::OnHeaderResult result_ =
        Http2VisitorInterface::HEADER_OK;
  };

  // Queues the connection preface, if not already done.
  void MaybeSetupPreface();

  void SendWindowUpdate(Http2StreamId stream_id, size_t update_delta);

  // Sends queued frames, returning true if all frames were flushed
  // successfully.
  bool SendQueuedFrames();

  // Returns false if the connection is write-blocked (due to flow control or
  // some other reason).
  bool WriteForStream(Http2StreamId stream_id);

  bool SendMetadata(Http2StreamId stream_id, MetadataSequence& sequence);

  void SendTrailers(Http2StreamId stream_id, spdy::SpdyHeaderBlock trailers);

  // Encapsulates the RST_STREAM NO_ERROR behavior described in RFC 7540
  // Section 8.1.
  void MaybeCloseWithRstStream(Http2StreamId stream_id, StreamState& state);

  // Performs flow control accounting for data sent by the peer.
  void MarkDataBuffered(Http2StreamId stream_id, size_t bytes);

  // Creates a stream and returns an iterator pointing to it.
  StreamStateMap::iterator CreateStream(Http2StreamId stream_id);

  // Receives events when inbound frames are parsed.
  Http2VisitorInterface& visitor_;

  // Encodes outbound frames.
  spdy::SpdyFramer framer_{spdy::SpdyFramer::ENABLE_COMPRESSION};

  // Decodes inbound frames.
  http2::Http2DecoderAdapter decoder_;

  // Maintains the state of all streams known to this session.
  StreamStateMap stream_map_;

  // Maintains the queue of outbound frames, and any serialized bytes that have
  // not yet been consumed.
  std::list<std::unique_ptr<spdy::SpdyFrameIR>> frames_;
  std::string serialized_prefix_;

  // Maintains the set of streams ready to write data to the peer.
  using WriteScheduler = PriorityWriteScheduler<Http2StreamId>;
  WriteScheduler write_scheduler_;

  // Delivers header name-value pairs to the visitor.
  PassthroughHeadersHandler headers_handler_;

  // Tracks the remaining client connection preface, in the case of a server
  // session.
  absl::string_view remaining_preface_;

  WindowManager connection_window_manager_;

  absl::flat_hash_set<Http2StreamId> streams_reset_;

  MetadataSequence connection_metadata_;

  Http2StreamId next_stream_id_ = 1;
  Http2StreamId highest_received_stream_id_ = 0;
  Http2StreamId metadata_stream_id_ = 0;
  size_t metadata_length_ = 0;
  int connection_send_window_ = kInitialFlowControlWindowSize;
  // The initial flow control receive window size for any newly created streams.
  int stream_receive_window_limit_ = kInitialFlowControlWindowSize;
  int max_frame_payload_ = 16384;
  Options options_;
  bool received_goaway_ = false;
  bool queued_preface_ = false;
  bool peer_supports_metadata_ = false;
  bool end_metadata_ = false;

  // Replace this with a stream ID, for multiple GOAWAY support.
  bool queued_goaway_ = false;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_
