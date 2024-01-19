// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Pre-defined mocks for the APIs in web_transport.h.

#ifndef QUICHE_WEB_TRANSPORT_TEST_TOOLS_MOCK_WEB_TRANSPORT_H_
#define QUICHE_WEB_TRANSPORT_TEST_TOOLS_MOCK_WEB_TRANSPORT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/web_transport/web_transport.h"

namespace webtransport {
namespace test {

class QUICHE_NO_EXPORT MockStreamVisitor : public StreamVisitor {
 public:
  MOCK_METHOD(void, OnCanRead, (), (override));
  MOCK_METHOD(void, OnCanWrite, (), (override));
  MOCK_METHOD(void, OnResetStreamReceived, (StreamErrorCode), (override));
  MOCK_METHOD(void, OnStopSendingReceived, (StreamErrorCode), (override));
  MOCK_METHOD(void, OnWriteSideInDataRecvdState, (), (override));
};

class QUICHE_NO_EXPORT MockStream : public Stream {
 public:
  MOCK_METHOD(ReadResult, Read, (absl::Span<char> buffer), (override));
  MOCK_METHOD(ReadResult, Read, (std::string * output), (override));
  MOCK_METHOD(absl::Status, Writev,
              (absl::Span<const absl::string_view> data,
               const quiche::StreamWriteOptions& options),
              (override));
  MOCK_METHOD(PeekResult, PeekNextReadableRegion, (), (const, override));
  MOCK_METHOD(bool, SkipBytes, (size_t bytes), (override));
  MOCK_METHOD(bool, CanWrite, (), (const, override));
  MOCK_METHOD(void, AbruptlyTerminate, (absl::Status), (override));
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
 public:
  MOCK_METHOD(void, OnSessionReady, (), (override));
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
  MOCK_METHOD(Stream*, GetStreamById, (StreamId), (override));
  MOCK_METHOD(DatagramStatus, SendOrQueueDatagram, (absl::string_view datagram),
              (override));
  MOCK_METHOD(uint64_t, GetMaxDatagramSize, (), (const, override));
  MOCK_METHOD(void, SetDatagramMaxTimeInQueue,
              (absl::Duration max_time_in_queue), (override));
  MOCK_METHOD(DatagramStats, GetDatagramStats, (), (override));
  MOCK_METHOD(SessionStats, GetSessionStats, (), (override));
  MOCK_METHOD(void, NotifySessionDraining, (), (override));
  MOCK_METHOD(void, SetOnDraining, (quiche::SingleUseCallback<void()>),
              (override));
};

}  // namespace test
}  // namespace webtransport

#endif  // QUICHE_WEB_TRANSPORT_TEST_TOOLS_MOCK_WEB_TRANSPORT_H_
