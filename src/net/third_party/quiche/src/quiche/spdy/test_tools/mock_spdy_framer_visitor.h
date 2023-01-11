// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_TEST_TOOLS_MOCK_SPDY_FRAMER_VISITOR_H_
#define QUICHE_SPDY_TEST_TOOLS_MOCK_SPDY_FRAMER_VISITOR_H_

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/spdy/core/http2_frame_decoder_adapter.h"
#include "quiche/spdy/core/recording_headers_handler.h"
#include "quiche/spdy/test_tools/spdy_test_utils.h"

namespace spdy {

namespace test {

class QUICHE_NO_EXPORT MockSpdyFramerVisitor
    : public SpdyFramerVisitorInterface {
 public:
  MockSpdyFramerVisitor();
  ~MockSpdyFramerVisitor() override;

  MOCK_METHOD(void, OnError,
              (http2::Http2DecoderAdapter::SpdyFramerError error,
               std::string detailed_error),
              (override));
  MOCK_METHOD(void, OnCommonHeader,
              (SpdyStreamId stream_id, size_t length, uint8_t type,
               uint8_t flags),
              (override));
  MOCK_METHOD(void, OnDataFrameHeader,
              (SpdyStreamId stream_id, size_t length, bool fin), (override));
  MOCK_METHOD(void, OnStreamFrameData,
              (SpdyStreamId stream_id, const char* data, size_t len),
              (override));
  MOCK_METHOD(void, OnStreamEnd, (SpdyStreamId stream_id), (override));
  MOCK_METHOD(void, OnStreamPadLength, (SpdyStreamId stream_id, size_t value),
              (override));
  MOCK_METHOD(void, OnStreamPadding, (SpdyStreamId stream_id, size_t len),
              (override));
  MOCK_METHOD(SpdyHeadersHandlerInterface*, OnHeaderFrameStart,
              (SpdyStreamId stream_id), (override));
  MOCK_METHOD(void, OnHeaderFrameEnd, (SpdyStreamId stream_id), (override));
  MOCK_METHOD(void, OnRstStream,
              (SpdyStreamId stream_id, SpdyErrorCode error_code), (override));
  MOCK_METHOD(void, OnSettings, (), (override));
  MOCK_METHOD(void, OnSetting, (SpdySettingsId id, uint32_t value), (override));
  MOCK_METHOD(void, OnPing, (SpdyPingId unique_id, bool is_ack), (override));
  MOCK_METHOD(void, OnSettingsEnd, (), (override));
  MOCK_METHOD(void, OnSettingsAck, (), (override));
  MOCK_METHOD(void, OnGoAway,
              (SpdyStreamId last_accepted_stream_id, SpdyErrorCode error_code),
              (override));
  MOCK_METHOD(bool, OnGoAwayFrameData, (const char* goaway_data, size_t len),
              (override));
  MOCK_METHOD(void, OnHeaders,
              (SpdyStreamId stream_id, size_t payload_length, bool has_priority,
               int weight, SpdyStreamId parent_stream_id, bool exclusive,
               bool fin, bool end),
              (override));
  MOCK_METHOD(void, OnWindowUpdate,
              (SpdyStreamId stream_id, int delta_window_size), (override));
  MOCK_METHOD(void, OnPushPromise,
              (SpdyStreamId stream_id, SpdyStreamId promised_stream_id,
               bool end),
              (override));
  MOCK_METHOD(void, OnContinuation,
              (SpdyStreamId stream_id, size_t payload_length, bool end),
              (override));
  MOCK_METHOD(
      void, OnAltSvc,
      (SpdyStreamId stream_id, absl::string_view origin,
       const SpdyAltSvcWireFormat::AlternativeServiceVector& altsvc_vector),
      (override));
  MOCK_METHOD(void, OnPriority,
              (SpdyStreamId stream_id, SpdyStreamId parent_stream_id,
               int weight, bool exclusive),
              (override));
  MOCK_METHOD(void, OnPriorityUpdate,
              (SpdyStreamId prioritized_stream_id,
               absl::string_view priority_field_value),
              (override));
  MOCK_METHOD(bool, OnUnknownFrame,
              (SpdyStreamId stream_id, uint8_t frame_type), (override));
  MOCK_METHOD(void, OnUnknownFrameStart,
              (SpdyStreamId stream_id, size_t length, uint8_t type,
               uint8_t flags),
              (override));
  MOCK_METHOD(void, OnUnknownFramePayload,
              (SpdyStreamId stream_id, absl::string_view payload), (override));

  void DelegateHeaderHandling() {
    ON_CALL(*this, OnHeaderFrameStart(testing::_))
        .WillByDefault(testing::Invoke(
            this, &MockSpdyFramerVisitor::ReturnTestHeadersHandler));
    ON_CALL(*this, OnHeaderFrameEnd(testing::_))
        .WillByDefault(testing::Invoke(
            this, &MockSpdyFramerVisitor::ResetTestHeadersHandler));
  }

  SpdyHeadersHandlerInterface* ReturnTestHeadersHandler(
      SpdyStreamId /* stream_id */) {
    if (headers_handler_ == nullptr) {
      headers_handler_ = std::make_unique<RecordingHeadersHandler>();
    }
    return headers_handler_.get();
  }

  void ResetTestHeadersHandler(SpdyStreamId /* stream_id */) {
    headers_handler_.reset();
  }

  std::unique_ptr<SpdyHeadersHandlerInterface> headers_handler_;
};

}  // namespace test

}  // namespace spdy

#endif  // QUICHE_SPDY_TEST_TOOLS_MOCK_SPDY_FRAMER_VISITOR_H_
