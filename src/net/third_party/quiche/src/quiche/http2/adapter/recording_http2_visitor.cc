#include "quiche/http2/adapter/recording_http2_visitor.h"

#include "absl/strings/str_format.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/http2_util.h"

namespace http2 {
namespace adapter {
namespace test {

int64_t RecordingHttp2Visitor::OnReadyToSend(absl::string_view serialized) {
  events_.push_back(absl::StrFormat("OnReadyToSend %d", serialized.size()));
  return serialized.size();
}

Http2VisitorInterface::DataFrameHeaderInfo
RecordingHttp2Visitor::OnReadyToSendDataForStream(Http2StreamId stream_id,
                                                  size_t max_length) {
  events_.push_back(absl::StrFormat("OnReadyToSendDataForStream %d %d",
                                    stream_id, max_length));
  return {70000, true, true};
}

bool RecordingHttp2Visitor::SendDataFrame(Http2StreamId stream_id,
                                          absl::string_view /*frame_header*/,
                                          size_t payload_bytes) {
  events_.push_back(
      absl::StrFormat("SendDataFrame %d %d", stream_id, payload_bytes));
  return true;
}

void RecordingHttp2Visitor::OnConnectionError(ConnectionError error) {
  events_.push_back(
      absl::StrFormat("OnConnectionError %s", ConnectionErrorToString(error)));
}

bool RecordingHttp2Visitor::OnFrameHeader(Http2StreamId stream_id,
                                          size_t length, uint8_t type,
                                          uint8_t flags) {
  events_.push_back(absl::StrFormat("OnFrameHeader %d %d %d %d", stream_id,
                                    length, type, flags));
  return true;
}

void RecordingHttp2Visitor::OnSettingsStart() {
  events_.push_back("OnSettingsStart");
}

void RecordingHttp2Visitor::OnSetting(Http2Setting setting) {
  events_.push_back(absl::StrFormat(
      "OnSetting %s %d", Http2SettingsIdToString(setting.id), setting.value));
}

void RecordingHttp2Visitor::OnSettingsEnd() {
  events_.push_back("OnSettingsEnd");
}

void RecordingHttp2Visitor::OnSettingsAck() {
  events_.push_back("OnSettingsAck");
}

bool RecordingHttp2Visitor::OnBeginHeadersForStream(Http2StreamId stream_id) {
  events_.push_back(absl::StrFormat("OnBeginHeadersForStream %d", stream_id));
  return true;
}

Http2VisitorInterface::OnHeaderResult RecordingHttp2Visitor::OnHeaderForStream(
    Http2StreamId stream_id, absl::string_view name, absl::string_view value) {
  events_.push_back(
      absl::StrFormat("OnHeaderForStream %d %s %s", stream_id, name, value));
  return HEADER_OK;
}

bool RecordingHttp2Visitor::OnEndHeadersForStream(Http2StreamId stream_id) {
  events_.push_back(absl::StrFormat("OnEndHeadersForStream %d", stream_id));
  return true;
}

bool RecordingHttp2Visitor::OnDataPaddingLength(Http2StreamId stream_id,
                                                size_t padding_length) {
  events_.push_back(
      absl::StrFormat("OnDataPaddingLength %d %d", stream_id, padding_length));
  return true;
}

bool RecordingHttp2Visitor::OnBeginDataForStream(Http2StreamId stream_id,
                                                 size_t payload_length) {
  events_.push_back(
      absl::StrFormat("OnBeginDataForStream %d %d", stream_id, payload_length));
  return true;
}

bool RecordingHttp2Visitor::OnDataForStream(Http2StreamId stream_id,
                                            absl::string_view data) {
  events_.push_back(absl::StrFormat("OnDataForStream %d %s", stream_id, data));
  return true;
}

bool RecordingHttp2Visitor::OnEndStream(Http2StreamId stream_id) {
  events_.push_back(absl::StrFormat("OnEndStream %d", stream_id));
  return true;
}

void RecordingHttp2Visitor::OnRstStream(Http2StreamId stream_id,
                                        Http2ErrorCode error_code) {
  events_.push_back(absl::StrFormat("OnRstStream %d %s", stream_id,
                                    Http2ErrorCodeToString(error_code)));
}

bool RecordingHttp2Visitor::OnCloseStream(Http2StreamId stream_id,
                                          Http2ErrorCode error_code) {
  events_.push_back(absl::StrFormat("OnCloseStream %d %s", stream_id,
                                    Http2ErrorCodeToString(error_code)));
  return true;
}

void RecordingHttp2Visitor::OnPriorityForStream(Http2StreamId stream_id,
                                                Http2StreamId parent_stream_id,
                                                int weight, bool exclusive) {
  events_.push_back(absl::StrFormat("OnPriorityForStream %d %d %d %d",
                                    stream_id, parent_stream_id, weight,
                                    exclusive));
}

void RecordingHttp2Visitor::OnPing(Http2PingId ping_id, bool is_ack) {
  events_.push_back(absl::StrFormat("OnPing %d %d", ping_id, is_ack));
}

void RecordingHttp2Visitor::OnPushPromiseForStream(
    Http2StreamId stream_id, Http2StreamId promised_stream_id) {
  events_.push_back(absl::StrFormat("OnPushPromiseForStream %d %d", stream_id,
                                    promised_stream_id));
}

bool RecordingHttp2Visitor::OnGoAway(Http2StreamId last_accepted_stream_id,
                                     Http2ErrorCode error_code,
                                     absl::string_view opaque_data) {
  events_.push_back(
      absl::StrFormat("OnGoAway %d %s %s", last_accepted_stream_id,
                      Http2ErrorCodeToString(error_code), opaque_data));
  return true;
}

void RecordingHttp2Visitor::OnWindowUpdate(Http2StreamId stream_id,
                                           int window_increment) {
  events_.push_back(
      absl::StrFormat("OnWindowUpdate %d %d", stream_id, window_increment));
}

int RecordingHttp2Visitor::OnBeforeFrameSent(uint8_t frame_type,
                                             Http2StreamId stream_id,
                                             size_t length, uint8_t flags) {
  events_.push_back(absl::StrFormat("OnBeforeFrameSent %d %d %d %d", frame_type,
                                    stream_id, length, flags));
  return 0;
}

int RecordingHttp2Visitor::OnFrameSent(uint8_t frame_type,
                                       Http2StreamId stream_id, size_t length,
                                       uint8_t flags, uint32_t error_code) {
  events_.push_back(absl::StrFormat("OnFrameSent %d %d %d %d %d", frame_type,
                                    stream_id, length, flags, error_code));
  return 0;
}

bool RecordingHttp2Visitor::OnInvalidFrame(Http2StreamId stream_id,
                                           InvalidFrameError error) {
  events_.push_back(absl::StrFormat("OnInvalidFrame %d %s", stream_id,
                                    InvalidFrameErrorToString(error)));
  return true;
}

void RecordingHttp2Visitor::OnBeginMetadataForStream(Http2StreamId stream_id,
                                                     size_t payload_length) {
  events_.push_back(absl::StrFormat("OnBeginMetadataForStream %d %d", stream_id,
                                    payload_length));
}

bool RecordingHttp2Visitor::OnMetadataForStream(Http2StreamId stream_id,
                                                absl::string_view metadata) {
  events_.push_back(
      absl::StrFormat("OnMetadataForStream %d %s", stream_id, metadata));
  return true;
}

bool RecordingHttp2Visitor::OnMetadataEndForStream(Http2StreamId stream_id) {
  events_.push_back(absl::StrFormat("OnMetadataEndForStream %d", stream_id));
  return true;
}

void RecordingHttp2Visitor::OnErrorDebug(absl::string_view message) {
  events_.push_back(absl::StrFormat("OnErrorDebug %s", message));
}

}  // namespace test
}  // namespace adapter
}  // namespace http2
