#ifndef QUICHE_HTTP2_ADAPTER_MOCK_HTTP2_VISITOR_INTERFACE_H_
#define QUICHE_HTTP2_ADAPTER_MOCK_HTTP2_VISITOR_INTERFACE_H_

#include "http2/adapter/http2_visitor_interface.h"
#include "common/platform/api/quiche_test.h"

namespace http2 {
namespace adapter {
namespace test {

// A mock visitor class, for use in tests.
class MockHttp2Visitor : public Http2VisitorInterface {
 public:
  MockHttp2Visitor() = default;

  MOCK_METHOD(void, OnConnectionError, (), (override));
  MOCK_METHOD(
      void,
      OnFrameHeader,
      (Http2StreamId stream_id, size_t length, uint8_t type, uint8_t flags),
      (override));
  MOCK_METHOD(void, OnSettingsStart, (), (override));
  MOCK_METHOD(void, OnSetting, (Http2Setting setting), (override));
  MOCK_METHOD(void, OnSettingsEnd, (), (override));
  MOCK_METHOD(void, OnSettingsAck, (), (override));
  MOCK_METHOD(void,
              OnBeginHeadersForStream,
              (Http2StreamId stream_id),
              (override));

  MOCK_METHOD(void,
              OnHeaderForStream,
              (Http2StreamId stream_id,
               absl::string_view key,
               absl::string_view value),
              (override));

  MOCK_METHOD(void,
              OnEndHeadersForStream,
              (Http2StreamId stream_id),
              (override));

  MOCK_METHOD(void,
              OnBeginDataForStream,
              (Http2StreamId stream_id, size_t payload_length),
              (override));

  MOCK_METHOD(void,
              OnDataForStream,
              (Http2StreamId stream_id, absl::string_view data),
              (override));

  MOCK_METHOD(void, OnEndStream, (Http2StreamId stream_id), (override));

  MOCK_METHOD(void,
              OnRstStream,
              (Http2StreamId stream_id, Http2ErrorCode error_code),
              (override));

  MOCK_METHOD(void,
              OnCloseStream,
              (Http2StreamId stream_id, Http2ErrorCode error_code),
              (override));

  MOCK_METHOD(void,
              OnPriorityForStream,
              (Http2StreamId stream_id,
               Http2StreamId parent_stream_id,
               int weight,
               bool exclusive),
              (override));

  MOCK_METHOD(void, OnPing, (Http2PingId ping_id, bool is_ack), (override));

  MOCK_METHOD(void,
              OnPushPromiseForStream,
              (Http2StreamId stream_id, Http2StreamId promised_stream_id),
              (override));

  MOCK_METHOD(void,
              OnGoAway,
              (Http2StreamId last_accepted_stream_id,
               Http2ErrorCode error_code,
               absl::string_view opaque_data),
              (override));

  MOCK_METHOD(void,
              OnWindowUpdate,
              (Http2StreamId stream_id, int window_increment),
              (override));

  MOCK_METHOD(void,
              OnReadyToSendDataForStream,
              (Http2StreamId stream_id,
               char* destination_buffer,
               size_t length,
               ssize_t* written,
               bool* end_stream),
              (override));

  MOCK_METHOD(
      void,
      OnReadyToSendMetadataForStream,
      (Http2StreamId stream_id, char* buffer, size_t length, ssize_t* written),
      (override));

  MOCK_METHOD(void,
              OnBeginMetadataForStream,
              (Http2StreamId stream_id, size_t payload_length),
              (override));

  MOCK_METHOD(void,
              OnMetadataForStream,
              (Http2StreamId stream_id, absl::string_view metadata),
              (override));

  MOCK_METHOD(void,
              OnMetadataEndForStream,
              (Http2StreamId stream_id),
              (override));
};

}  // namespace test
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_MOCK_HTTP2_VISITOR_INTERFACE_H_
