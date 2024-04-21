// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_MOCK_VISITOR_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOQT_MOCK_VISITOR_H_

#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/platform/api/quic_test.h"

namespace moqt::test {

struct MockSessionCallbacks {
  testing::MockFunction<void()> session_established_callback;
  testing::MockFunction<void(absl::string_view)> session_terminated_callback;
  testing::MockFunction<void()> session_deleted_callback;
  testing::MockFunction<std::optional<MoqtAnnounceErrorReason>(
      absl::string_view)>
      incoming_announce_callback;

  MockSessionCallbacks() {
    ON_CALL(incoming_announce_callback, Call(testing::_))
        .WillByDefault(DefaultIncomingAnnounceCallback);
  }

  MoqtSessionCallbacks AsSessionCallbacks() {
    return MoqtSessionCallbacks{session_established_callback.AsStdFunction(),
                                session_terminated_callback.AsStdFunction(),
                                session_deleted_callback.AsStdFunction(),
                                incoming_announce_callback.AsStdFunction()};
  }
};

class MockLocalTrackVisitor : public LocalTrack::Visitor {
 public:
  MOCK_METHOD(std::optional<absl::string_view>, OnSubscribeForPast,
              (const SubscribeWindow& window), (override));
};

class MockRemoteTrackVisitor : public RemoteTrack::Visitor {
 public:
  MOCK_METHOD(void, OnReply,
              (const FullTrackName& full_track_name,
               std::optional<absl::string_view> error_reason_phrase),
              (override));
  MOCK_METHOD(void, OnObjectFragment,
              (const FullTrackName& full_track_name, uint64_t group_sequence,
               uint64_t object_sequence, uint64_t object_send_order,
               MoqtForwardingPreference forwarding_preference,
               absl::string_view object, bool end_of_message),
              (override));
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_MOCK_VISITOR_H_
