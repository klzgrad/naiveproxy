// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOCK_MOQT_SESSION_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOCK_MOQT_SESSION_H_

#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {
namespace test {

class MockMoqtSession : public MoqtSessionInterface {
 public:
  MOCK_METHOD(MoqtSessionCallbacks&, callbacks, (), (override));
  MOCK_METHOD(void, Error, (MoqtError code, absl::string_view error),
              (override));
  MOCK_METHOD(bool, SubscribeAbsolute,
              (const FullTrackName& name, uint64_t start_group,
               uint64_t start_object, SubscribeVisitor* visitor,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, SubscribeAbsolute,
              (const FullTrackName& name, uint64_t start_group,
               uint64_t start_object, uint64_t end_group,
               SubscribeVisitor* visitor, VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, SubscribeCurrentObject,
              (const FullTrackName& name, SubscribeVisitor* visitor,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, SubscribeNextGroup,
              (const FullTrackName& name, SubscribeVisitor* visitor,
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
  MOCK_METHOD(bool, RelativeJoiningFetch,
              (const FullTrackName& name, SubscribeVisitor* visitor,
               uint64_t num_previous_groups,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, RelativeJoiningFetch,
              (const FullTrackName& name, SubscribeVisitor* visitor,
               FetchResponseCallback callback, uint64_t num_previous_groups,
               MoqtPriority priority,
               std::optional<MoqtDeliveryOrder> delivery_order,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(void, PublishNamespace,
              (TrackNamespace track_namespace,
               MoqtOutgoingPublishNamespaceCallback callback,
               VersionSpecificParameters parameters),
              (override));
  MOCK_METHOD(bool, PublishNamespaceDone, (TrackNamespace track_namespace),
              (override));

  quiche::QuicheWeakPtr<MoqtSessionInterface> GetWeakPtr() override {
    return weak_factory_.Create();
  }
  quiche::QuicheWeakPtrFactory<MoqtSessionInterface> weak_factory_{this};
};

}  // namespace test
}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOCK_MOQT_SESSION_H_
