// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_known_track_publisher.h"

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"

namespace moqt {

absl::StatusOr<std::shared_ptr<MoqtTrackPublisher>>
MoqtKnownTrackPublisher::GetTrack(const FullTrackName& track_name) {
  auto it = tracks_.find(track_name);
  if (it == tracks_.end()) {
    return absl::NotFoundError("Requested track not found");
  }
  return it->second;
}

void MoqtKnownTrackPublisher::Add(
    std::shared_ptr<MoqtTrackPublisher> track_publisher) {
  const FullTrackName& track_name = track_publisher->GetTrackName();
  auto [it, success] = tracks_.emplace(track_name, track_publisher);
  QUICHE_BUG_IF(MoqtKnownTrackPublisher_duplicate, !success)
      << "Trying to add a duplicate track into a KnownTrackPublisher";
}

void MoqtKnownTrackPublisher::Delete(const FullTrackName& track_name) {
  tracks_.erase(track_name);
}

}  // namespace moqt
