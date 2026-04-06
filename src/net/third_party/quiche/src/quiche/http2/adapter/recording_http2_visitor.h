#ifndef QUICHE_HTTP2_ADAPTER_RECORDING_HTTP2_VISITOR_H_
#define QUICHE_HTTP2_ADAPTER_RECORDING_HTTP2_VISITOR_H_

#include <cstdint>
#include <list>
#include <string>

#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace http2 {
namespace adapter {
namespace test {

// A visitor implementation that records the sequence of callbacks it receives.
class QUICHE_NO_EXPORT RecordingHttp2Visitor : public Http2VisitorInterface {
 public:
  using Event = std::string;
  using EventSequence = std::list<Event>;

  // From Http2VisitorInterface
  int64_t OnReadyToSend(absl::string_view serialized) override;
  DataFrameHeaderInfo OnReadyToSendDataForStream(Http2StreamId stream_id,
                                                 size_t max_length) override;
  bool SendDataFrame(Http2StreamId stream_id, absl::string_view frame_header,
                     size_t payload_bytes) override;
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
  std::pair<int64_t, bool> PackMetadataForStream(Http2StreamId stream_id,
                                                 uint8_t* dest,
                                                 size_t dest_len) override;
  void OnErrorDebug(absl::string_view message) override;

  const EventSequence& GetEventSequence() const { return events_; }
  void Clear() { events_.clear(); }

 private:
  EventSequence events_;
};

}  // namespace test
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_RECORDING_HTTP2_VISITOR_H_
