// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_MOCK_SPDY_FRAMER_VISITOR_H_
#define QUICHE_SPDY_CORE_MOCK_SPDY_FRAMER_VISITOR_H_

#include <cstdint>
#include <memory>
#include <utility>

#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"
#include "net/third_party/quiche/src/spdy/core/http2_frame_decoder_adapter.h"
#include "net/third_party/quiche/src/spdy/core/spdy_test_utils.h"

namespace spdy {

namespace test {

class MockSpdyFramerVisitor : public SpdyFramerVisitorInterface {
 public:
  MockSpdyFramerVisitor();
  ~MockSpdyFramerVisitor() override;

  MOCK_METHOD(void,
              OnError,
              (http2::Http2DecoderAdapter::SpdyFramerError error,
               std::string detailed_error),
              (override));
  MOCK_METHOD(void,
              OnDataFrameHeader,
              (SpdyStreamId stream_id, size_t length, bool fin),
              (override));
  MOCK_METHOD(void,
              OnStreamFrameData,
              (SpdyStreamId stream_id, const char* data, size_t len),
              (override));
  MOCK_METHOD(void, OnStreamEnd, (SpdyStreamId stream_id), (override));
  MOCK_METHOD(void,
              OnStreamPadLength,
              (SpdyStreamId stream_id, size_t value),
              (override));
  MOCK_METHOD(void,
              OnStreamPadding,
              (SpdyStreamId stream_id, size_t len),
              (override));
  MOCK_METHOD(SpdyHeadersHandlerInterface*,
              OnHeaderFrameStart,
              (SpdyStreamId stream_id),
              (override));
  MOCK_METHOD(void, OnHeaderFrameEnd, (SpdyStreamId stream_id), (override));
  MOCK_METHOD(void,
              OnRstStream,
              (SpdyStreamId stream_id, SpdyErrorCode error_code),
              (override));
  MOCK_METHOD(void, OnSettings, (), (override));
  MOCK_METHOD(void, OnSetting, (SpdySettingsId id, uint32_t value), (override));
  MOCK_METHOD(void, OnPing, (SpdyPingId unique_id, bool is_ack), (override));
  MOCK_METHOD(void, OnSettingsEnd, (), (override));
  MOCK_METHOD(void,
              OnGoAway,
              (SpdyStreamId last_accepted_stream_id, SpdyErrorCode error_code),
              (override));
  MOCK_METHOD(void,
              OnHeaders,
              (SpdyStreamId stream_id,
               bool has_priority,
               int weight,
               SpdyStreamId parent_stream_id,
               bool exclusive,
               bool fin,
               bool end),
              (override));
  MOCK_METHOD(void,
              OnWindowUpdate,
              (SpdyStreamId stream_id, int delta_window_size),
              (override));
  MOCK_METHOD(void,
              OnPushPromise,
              (SpdyStreamId stream_id,
               SpdyStreamId promised_stream_id,
               bool end),
              (override));
  MOCK_METHOD(void,
              OnContinuation,
              (SpdyStreamId stream_id, bool end),
              (override));
  MOCK_METHOD(
      void,
      OnAltSvc,
      (SpdyStreamId stream_id,
       quiche::QuicheStringPiece origin,
       const SpdyAltSvcWireFormat::AlternativeServiceVector& altsvc_vector),
      (override));
  MOCK_METHOD(void,
              OnPriority,
              (SpdyStreamId stream_id,
               SpdyStreamId parent_stream_id,
               int weight,
               bool exclusive),
              (override));
  MOCK_METHOD(bool,
              OnUnknownFrame,
              (SpdyStreamId stream_id, uint8_t frame_type),
              (override));

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
      headers_handler_ = std::make_unique<TestHeadersHandler>();
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

#endif  // QUICHE_SPDY_CORE_MOCK_SPDY_FRAMER_VISITOR_H_
