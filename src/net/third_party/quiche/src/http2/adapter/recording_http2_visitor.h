#ifndef QUICHE_HTTP2_ADAPTER_RECORDING_HTTP2_VISITOR_H_
#define QUICHE_HTTP2_ADAPTER_RECORDING_HTTP2_VISITOR_H_

#include <list>
#include <string>

#include "http2/adapter/http2_visitor_interface.h"
#include "common/platform/api/quiche_test.h"

namespace http2 {
namespace adapter {
namespace test {

// A visitor implementation that records the sequence of callbacks it receives.
class RecordingHttp2Visitor : public Http2VisitorInterface {
 public:
  using Event = std::string;
  using EventSequence = std::list<Event>;

  // From Http2VisitorInterface
  void OnConnectionError() override;
  void OnFrameHeader(Http2StreamId stream_id,
                     size_t length,
                     uint8_t type,
                     uint8_t flags) override;
  void OnSettingsStart() override;
  void OnSetting(Http2Setting setting) override;
  void OnSettingsEnd() override;
  void OnSettingsAck() override;
  void OnBeginHeadersForStream(Http2StreamId stream_id) override;
  void OnHeaderForStream(Http2StreamId stream_id,
                         absl::string_view name,
                         absl::string_view value) override;
  void OnEndHeadersForStream(Http2StreamId stream_id) override;
  void OnBeginDataForStream(Http2StreamId stream_id,
                            size_t payload_length) override;
  void OnDataForStream(Http2StreamId stream_id,
                       absl::string_view data) override;
  void OnEndStream(Http2StreamId stream_id) override;
  void OnRstStream(Http2StreamId stream_id, Http2ErrorCode error_code) override;
  void OnCloseStream(Http2StreamId stream_id,
                     Http2ErrorCode error_code) override;
  void OnPriorityForStream(Http2StreamId stream_id,
                           Http2StreamId parent_stream_id,
                           int weight,
                           bool exclusive) override;
  void OnPing(Http2PingId ping_id, bool is_ack) override;
  void OnPushPromiseForStream(Http2StreamId stream_id,
                              Http2StreamId promised_stream_id) override;
  void OnGoAway(Http2StreamId last_accepted_stream_id,
                Http2ErrorCode error_code,
                absl::string_view opaque_data) override;
  void OnWindowUpdate(Http2StreamId stream_id, int window_increment) override;
  void OnReadyToSendDataForStream(Http2StreamId stream_id,
                                  char* destination_buffer,
                                  size_t length,
                                  ssize_t* written,
                                  bool* end_stream) override;
  void OnReadyToSendMetadataForStream(Http2StreamId stream_id,
                                      char* buffer,
                                      size_t length,
                                      ssize_t* written) override;
  void OnBeginMetadataForStream(Http2StreamId stream_id,
                                size_t payload_length) override;
  void OnMetadataForStream(Http2StreamId stream_id,
                           absl::string_view metadata) override;
  void OnMetadataEndForStream(Http2StreamId stream_id) override;

  const EventSequence& GetEventSequence() const { return events_; }
  void Clear() { events_.clear(); }

 private:
  EventSequence events_;
};

}  // namespace test
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_RECORDING_HTTP2_VISITOR_H_
