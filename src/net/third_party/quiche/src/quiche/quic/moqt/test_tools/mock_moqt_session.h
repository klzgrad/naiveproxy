// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOCK_MOQT_SESSION_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOCK_MOQT_SESSION_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace moqt::test {

// Mock version of MoqtSession.  If `publisher` is provided via constructor, all
// of the SUBSCRIBE and FETCH requests are routed towards it.
class MockMoqtSession : public MoqtSessionInterface {
 public:
  explicit MockMoqtSession(MoqtPublisher* publisher);
  ~MockMoqtSession() override;

  MockMoqtSession(const MockMoqtSession&) = delete;
  MockMoqtSession(MockMoqtSession&&) = delete;
  MockMoqtSession& operator=(const MockMoqtSession&) = delete;
  MockMoqtSession& operator=(MockMoqtSession&&) = delete;

  MoqtSessionCallbacks& callbacks() override { return callbacks_; }

  MOCK_METHOD(void, Error, (MoqtError code, absl::string_view error),
              (override));
  MOCK_METHOD(bool, SubscribeAbsolute,
              (const FullTrackName& name, uint64_t start_group,
               uint64_t start_object, SubscribeRemoteTrack::Visitor* visitor,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, SubscribeAbsolute,
              (const FullTrackName& name, uint64_t start_group,
               uint64_t start_object, uint64_t end_group,
               SubscribeRemoteTrack::Visitor* visitor,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, SubscribeCurrentObject,
              (const FullTrackName& name,
               SubscribeRemoteTrack::Visitor* visitor,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, SubscribeNextGroup,
              (const FullTrackName& name,
               SubscribeRemoteTrack::Visitor* visitor,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, SubscribeUpdate,
              (const FullTrackName& name, std::optional<Location> start,
               std::optional<uint64_t> end_group,
               std::optional<MoqtPriority> subscriber_priority,
               std::optional<bool> forward,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(void, Unsubscribe, (const FullTrackName& name), (override));
  MOCK_METHOD(bool, Fetch,
              (const FullTrackName& name, FetchResponseCallback callback,
               Location start, uint64_t end_group,
               std::optional<uint64_t> end_object, MoqtPriority priority,
               std::optional<MoqtDeliveryOrder> delivery_order,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, JoiningFetch,
              (const FullTrackName& name,
               SubscribeRemoteTrack::Visitor* visitor,
               uint64_t num_previous_groups,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, JoiningFetch,
              (const FullTrackName& name,
               SubscribeRemoteTrack::Visitor* visitor,
               FetchResponseCallback callback, uint64_t num_previous_groups,
               MoqtPriority priority,
               std::optional<MoqtDeliveryOrder> delivery_order,
               VersionSpecificParameters parameters),
              (override));

 private:
  class LoopbackObjectListener;

  bool Subscribe(const FullTrackName& name,
                 SubscribeRemoteTrack::Visitor* visitor,
                 SubscribeWindow window);

  MoqtPublisher* const publisher_ = nullptr;
  MoqtSessionCallbacks callbacks_;
  absl::flat_hash_map<FullTrackName, std::unique_ptr<LoopbackObjectListener>>
      receiving_subscriptions_;
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOCK_MOQT_SESSION_H_
