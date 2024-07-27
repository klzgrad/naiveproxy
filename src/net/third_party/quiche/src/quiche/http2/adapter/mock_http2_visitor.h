#ifndef QUICHE_HTTP2_ADAPTER_MOCK_HTTP2_VISITOR_H_
#define QUICHE_HTTP2_ADAPTER_MOCK_HTTP2_VISITOR_H_

#include <cstdint>

#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace http2 {
namespace adapter {
namespace test {

// A mock visitor class, for use in tests.
class QUICHE_NO_EXPORT MockHttp2Visitor : public Http2VisitorInterface {
 public:
  MockHttp2Visitor() {
    ON_CALL(*this, OnFrameHeader).WillByDefault(testing::Return(true));
    ON_CALL(*this, OnBeginHeadersForStream)
        .WillByDefault(testing::Return(true));
    ON_CALL(*this, OnHeaderForStream).WillByDefault(testing::Return(HEADER_OK));
    ON_CALL(*this, OnEndHeadersForStream).WillByDefault(testing::Return(true));
    ON_CALL(*this, OnDataPaddingLength).WillByDefault(testing::Return(true));
    ON_CALL(*this, OnBeginDataForStream).WillByDefault(testing::Return(true));
    ON_CALL(*this, OnDataForStream).WillByDefault(testing::Return(true));
    ON_CALL(*this, OnEndStream).WillByDefault(testing::Return(true));
    ON_CALL(*this, OnCloseStream).WillByDefault(testing::Return(true));
    ON_CALL(*this, OnGoAway).WillByDefault(testing::Return(true));
    ON_CALL(*this, OnInvalidFrame).WillByDefault(testing::Return(true));
    ON_CALL(*this, OnMetadataForStream).WillByDefault(testing::Return(true));
    ON_CALL(*this, OnMetadataEndForStream).WillByDefault(testing::Return(true));
  }

  MOCK_METHOD(int64_t, OnReadyToSend, (absl::string_view serialized),
              (override));
  MOCK_METHOD(DataFrameHeaderInfo, OnReadyToSendDataForStream,
              (Http2StreamId stream_id, size_t max_length), (override));
  MOCK_METHOD(bool, SendDataFrame,
              (Http2StreamId stream_id, absl::string_view frame_header,
               size_t payload_bytes),
              (override));
  MOCK_METHOD(void, OnConnectionError, (ConnectionError error), (override));
  MOCK_METHOD(bool, OnFrameHeader,
              (Http2StreamId stream_id, size_t length, uint8_t type,
               uint8_t flags),
              (override));
  MOCK_METHOD(void, OnSettingsStart, (), (override));
  MOCK_METHOD(void, OnSetting, (Http2Setting setting), (override));
  MOCK_METHOD(void, OnSettingsEnd, (), (override));
  MOCK_METHOD(void, OnSettingsAck, (), (override));
  MOCK_METHOD(bool, OnBeginHeadersForStream, (Http2StreamId stream_id),
              (override));

  MOCK_METHOD(OnHeaderResult, OnHeaderForStream,
              (Http2StreamId stream_id, absl::string_view key,
               absl::string_view value),
              (override));

  MOCK_METHOD(bool, OnEndHeadersForStream, (Http2StreamId stream_id),
              (override));

  MOCK_METHOD(bool, OnDataPaddingLength,
              (Http2StreamId strema_id, size_t padding_length), (override));

  MOCK_METHOD(bool, OnBeginDataForStream,
              (Http2StreamId stream_id, size_t payload_length), (override));

  MOCK_METHOD(bool, OnDataForStream,
              (Http2StreamId stream_id, absl::string_view data), (override));

  MOCK_METHOD(bool, OnEndStream, (Http2StreamId stream_id), (override));

  MOCK_METHOD(void, OnRstStream,
              (Http2StreamId stream_id, Http2ErrorCode error_code), (override));

  MOCK_METHOD(bool, OnCloseStream,
              (Http2StreamId stream_id, Http2ErrorCode error_code), (override));

  MOCK_METHOD(void, OnPriorityForStream,
              (Http2StreamId stream_id, Http2StreamId parent_stream_id,
               int weight, bool exclusive),
              (override));

  MOCK_METHOD(void, OnPing, (Http2PingId ping_id, bool is_ack), (override));

  MOCK_METHOD(void, OnPushPromiseForStream,
              (Http2StreamId stream_id, Http2StreamId promised_stream_id),
              (override));

  MOCK_METHOD(bool, OnGoAway,
              (Http2StreamId last_accepted_stream_id, Http2ErrorCode error_code,
               absl::string_view opaque_data),
              (override));

  MOCK_METHOD(void, OnWindowUpdate,
              (Http2StreamId stream_id, int window_increment), (override));

  MOCK_METHOD(int, OnBeforeFrameSent,
              (uint8_t frame_type, Http2StreamId stream_id, size_t length,
               uint8_t flags),
              (override));

  MOCK_METHOD(int, OnFrameSent,
              (uint8_t frame_type, Http2StreamId stream_id, size_t length,
               uint8_t flags, uint32_t error_code),
              (override));

  MOCK_METHOD(bool, OnInvalidFrame,
              (Http2StreamId stream_id, InvalidFrameError error), (override));

  MOCK_METHOD(void, OnBeginMetadataForStream,
              (Http2StreamId stream_id, size_t payload_length), (override));

  MOCK_METHOD(bool, OnMetadataForStream,
              (Http2StreamId stream_id, absl::string_view metadata),
              (override));

  MOCK_METHOD(bool, OnMetadataEndForStream, (Http2StreamId stream_id),
              (override));

  MOCK_METHOD(void, OnErrorDebug, (absl::string_view message), (override));
};

}  // namespace test
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_MOCK_HTTP2_VISITOR_H_
