#ifndef QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_
#define QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_

#include <list>

#include "http2/adapter/http2_session.h"
#include "http2/adapter/http2_util.h"
#include "http2/adapter/http2_visitor_interface.h"
#include "http2/adapter/window_manager.h"
#include "common/platform/api/quiche_bug_tracker.h"
#include "spdy/core/http2_frame_decoder_adapter.h"
#include "spdy/core/spdy_framer.h"

namespace http2 {
namespace adapter {

// This class manages state associated with a single multiplexed HTTP/2 session.
class OgHttp2Session : public Http2Session,
                       public spdy::SpdyFramerVisitorInterface {
 public:
  struct Options {
    Perspective perspective = Perspective::kClient;
  };

  OgHttp2Session(Http2VisitorInterface& visitor, Options /*options*/);
  ~OgHttp2Session() override;

  // Enqueues a frame for transmission to the peer.
  void EnqueueFrame(std::unique_ptr<spdy::SpdyFrameIR> frame);

  // If |want_write()| returns true, this method will return a non-empty string
  // containing serialized HTTP/2 frames to write to the peer.
  std::string GetBytesToWrite(absl::optional<size_t> max_bytes);

  // From Http2Session.
  ssize_t ProcessBytes(absl::string_view bytes) override;
  int Consume(Http2StreamId stream_id, size_t num_bytes) override;
  bool want_read() const override { return !received_goaway_; }
  bool want_write() const override {
    return !frames_.empty() || !serialized_prefix_.empty();
  }
  int GetRemoteWindowSize() const override {
    return peer_window_;
  }

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
  bool OnGoAwayFrameData(const char* goaway_data, size_t len);
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
  void OnAltSvc(spdy::SpdyStreamId /*stream_id*/,
                absl::string_view /*origin*/,
                const spdy::SpdyAltSvcWireFormat::
                    AlternativeServiceVector& /*altsvc_vector*/);
  void OnPriority(spdy::SpdyStreamId stream_id,
                  spdy::SpdyStreamId parent_stream_id,
                  int weight,
                  bool exclusive) override;
  void OnPriorityUpdate(spdy::SpdyStreamId prioritized_stream_id,
                        absl::string_view priority_field_value) override;
  bool OnUnknownFrame(spdy::SpdyStreamId stream_id,
                      uint8_t frame_type) override;

 private:
  struct StreamState {
    WindowManager window_manager;
    int32_t send_window = 65535;
    bool half_closed_local = false;
    bool half_closed_remote = false;
  };

  class PassthroughHeadersHandler : public spdy::SpdyHeadersHandlerInterface {
   public:
    explicit PassthroughHeadersHandler(Http2VisitorInterface& visitor)
        : visitor_(visitor) {}
    void set_stream_id(Http2StreamId stream_id) { stream_id_ = stream_id; }
    void OnHeaderBlockStart() override;
    void OnHeader(absl::string_view key, absl::string_view value) override;
    void OnHeaderBlockEnd(size_t /* uncompressed_header_bytes */,
                          size_t /* compressed_header_bytes */) override;

   private:
    Http2VisitorInterface& visitor_;
    Http2StreamId stream_id_ = 0;
  };

  Http2VisitorInterface& visitor_;
  spdy::SpdyFramer framer_{spdy::SpdyFramer::ENABLE_COMPRESSION};
  http2::Http2DecoderAdapter decoder_;
  absl::flat_hash_map<Http2StreamId, StreamState> stream_map_;
  std::list<std::unique_ptr<spdy::SpdyFrameIR>> frames_;
  PassthroughHeadersHandler headers_handler_;
  std::string serialized_prefix_;
  absl::string_view remaining_preface_;
  int peer_window_ = 65535;
  Options options_;
  bool received_goaway_ = false;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_OGHTTP2_SESSION_H_
