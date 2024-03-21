#ifndef QUICHE_HTTP2_ADAPTER_CALLBACK_VISITOR_H_
#define QUICHE_HTTP2_ADAPTER_CALLBACK_VISITOR_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/nghttp2.h"
#include "quiche/http2/adapter/nghttp2_util.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace http2 {
namespace adapter {

// This visitor implementation accepts a set of nghttp2 callbacks and a "user
// data" pointer, and invokes the callbacks according to HTTP/2 events received.
class QUICHE_EXPORT CallbackVisitor : public Http2VisitorInterface {
 public:
  // Called when the visitor receives a close event for `stream_id`.
  using StreamCloseListener =
      quiche::MultiUseCallback<void(Http2StreamId stream_id)>;

  explicit CallbackVisitor(Perspective perspective,
                           const nghttp2_session_callbacks& callbacks,
                           void* user_data);

  int64_t OnReadyToSend(absl::string_view serialized) override;
  void OnConnectionError(ConnectionError error) override;
  bool OnFrameHeader(Http2StreamId stream_id, size_t length, uint8_t type,
                     uint8_t flags) override;
  void OnSettingsStart() override;
  void OnSetting(Http2Setting setting) override;
  void OnSettingsEnd() override;
  void OnSettingsAck() override;
  bool OnBeginHeadersForStream(Http2StreamId stream_id) override;
  OnHeaderResult OnHeaderForStream(Http2StreamId stream_id,
                                   absl::string_view name,
                                   absl::string_view value) override;
  bool OnEndHeadersForStream(Http2StreamId stream_id) override;
  bool OnDataPaddingLength(Http2StreamId stream_id,
                           size_t padding_length) override;
  bool OnBeginDataForStream(Http2StreamId stream_id,
                            size_t payload_length) override;
  bool OnDataForStream(Http2StreamId stream_id,
                       absl::string_view data) override;
  bool OnEndStream(Http2StreamId stream_id) override;
  void OnRstStream(Http2StreamId stream_id, Http2ErrorCode error_code) override;
  bool OnCloseStream(Http2StreamId stream_id,
                     Http2ErrorCode error_code) override;
  void OnPriorityForStream(Http2StreamId stream_id,
                           Http2StreamId parent_stream_id, int weight,
                           bool exclusive) override;
  void OnPing(Http2PingId ping_id, bool is_ack) override;
  void OnPushPromiseForStream(Http2StreamId stream_id,
                              Http2StreamId promised_stream_id) override;
  bool OnGoAway(Http2StreamId last_accepted_stream_id,
                Http2ErrorCode error_code,
                absl::string_view opaque_data) override;
  void OnWindowUpdate(Http2StreamId stream_id, int window_increment) override;
  int OnBeforeFrameSent(uint8_t frame_type, Http2StreamId stream_id,
                        size_t length, uint8_t flags) override;
  int OnFrameSent(uint8_t frame_type, Http2StreamId stream_id, size_t length,
                  uint8_t flags, uint32_t error_code) override;
  bool OnInvalidFrame(Http2StreamId stream_id,
                      InvalidFrameError error) override;
  void OnBeginMetadataForStream(Http2StreamId stream_id,
                                size_t payload_length) override;
  bool OnMetadataForStream(Http2StreamId stream_id,
                           absl::string_view metadata) override;
  bool OnMetadataEndForStream(Http2StreamId stream_id) override;
  void OnErrorDebug(absl::string_view message) override;

  size_t stream_map_size() const { return stream_map_.size(); }

  void set_stream_close_listener(StreamCloseListener stream_close_listener) {
    stream_close_listener_ = std::move(stream_close_listener);
  }

 private:
  struct QUICHE_EXPORT StreamInfo {
    bool before_sent_headers = false;
    bool sent_headers = false;
    bool received_headers = false;
  };

  using StreamInfoMap = absl::flat_hash_map<Http2StreamId, StreamInfo>;

  void PopulateFrame(nghttp2_frame& frame, uint8_t frame_type,
                     Http2StreamId stream_id, size_t length, uint8_t flags,
                     uint32_t error_code, bool sent_headers);

  // Creates the StreamInfoMap entry if it doesn't exist.
  StreamInfoMap::iterator GetStreamInfo(Http2StreamId stream_id);

  StreamInfoMap stream_map_;

  StreamCloseListener stream_close_listener_;

  Perspective perspective_;
  nghttp2_session_callbacks_unique_ptr callbacks_;
  void* user_data_;

  nghttp2_frame current_frame_;
  std::vector<nghttp2_settings_entry> settings_;
  size_t remaining_data_ = 0;
  // Any new stream must have an ID greater than the watermark.
  Http2StreamId stream_id_watermark_ = 0;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_CALLBACK_VISITOR_H_
