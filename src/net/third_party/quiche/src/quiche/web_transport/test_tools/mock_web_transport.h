// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Pre-defined mocks for the APIs in web_transport.h.

#ifndef QUICHE_WEB_TRANSPORT_TEST_TOOLS_MOCK_WEB_TRANSPORT_H_
#define QUICHE_WEB_TRANSPORT_TEST_TOOLS_MOCK_WEB_TRANSPORT_H_

#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/web_transport/web_transport.h"

namespace webtransport {
namespace test {

class QUICHE_NO_EXPORT MockStreamVisitor : public StreamVisitor {
  MOCK_METHOD(void, OnCanRead, (), (override));
  MOCK_METHOD(void, OnCanWrite, (), (override));
  MOCK_METHOD(void, OnResetStreamReceived, (StreamErrorCode), (override));
  MOCK_METHOD(void, OnStopSendingReceived, (StreamErrorCode), (override));
  MOCK_METHOD(void, OnWriteSideInDataRecvdState, (), (override));
};

class QUICHE_NO_EXPORT MockStream : public Stream {
  MOCK_METHOD(ReadResult, Read, (absl::Span<char> buffer), (override));
  MOCK_METHOD(ReadResult, Read, (std::string * output), (override));
  MOCK_METHOD(absl::Status, Writev,
              (absl::Span<const absl::string_view> data,
               const quiche::StreamWriteOptions& options),
              (override));
  MOCK_METHOD(bool, CanWrite, (), (const, override));
  MOCK_METHOD(size_t, ReadableBytes, (), (const, override));
  MOCK_METHOD(StreamId, GetStreamId, (), (const, override));
  MOCK_METHOD(void, ResetWithUserCode, (StreamErrorCode error), (override));
  MOCK_METHOD(void, SendStopSending, (StreamErrorCode error), (override));
  MOCK_METHOD(void, ResetDueToInternalError, (), (override));
  MOCK_METHOD(void, MaybeResetDueToStreamObjectGone, (), (override));
  MOCK_METHOD(StreamVisitor*, visitor, (), (override));
  MOCK_METHOD(void, SetVisitor, (std::unique_ptr<StreamVisitor> visitor),
              (override));
};

class QUICHE_NO_EXPORT MockSessionVisitor : public SessionVisitor {
  MOCK_METHOD(void, OnSessionReady, (const spdy::Http2HeaderBlock& headers),
              (override));
  MOCK_METHOD(void, OnSessionClosed,
              (SessionErrorCode error_code, const std::string& error_message),
              (override));
  MOCK_METHOD(void, OnIncomingBidirectionalStreamAvailable, (), (override));
  MOCK_METHOD(void, OnIncomingUnidirectionalStreamAvailable, (), (override));
  MOCK_METHOD(void, OnDatagramReceived, (absl::string_view datagram),
              (override));
  MOCK_METHOD(void, OnCanCreateNewOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(void, OnCanCreateNewOutgoingUnidirectionalStream, (), (override));
};

class QUICHE_NO_EXPORT MockSession : public Session {
 public:
  MOCK_METHOD(void, CloseSession,
              (SessionErrorCode error_code, absl::string_view error_message),
              (override));
  MOCK_METHOD(Stream*, AcceptIncomingBidirectionalStream, (), (override));
  MOCK_METHOD(Stream*, AcceptIncomingUnidirectionalStream, (), (override));
  MOCK_METHOD(bool, CanOpenNextOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(bool, CanOpenNextOutgoingUnidirectionalStream, (), (override));
  MOCK_METHOD(Stream*, OpenOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(Stream*, OpenOutgoingUnidirectionalStream, (), (override));
  MOCK_METHOD(DatagramStatus, SendOrQueueDatagram, (absl::string_view datagram),
              (override));
  MOCK_METHOD(size_t, GetMaxDatagramSize, (), (const, override));
  MOCK_METHOD(void, SetDatagramMaxTimeInQueue,
              (absl::Duration max_time_in_queue), (override));
};

}  // namespace test
}  // namespace webtransport

#endif  // QUICHE_WEB_TRANSPORT_TEST_TOOLS_MOCK_WEB_TRANSPORT_H_
