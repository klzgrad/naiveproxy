// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_subscribe_windows.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

bool SubscribeWindow::InWindow(const FullSequence& seq) const {
  if (seq < start_) {
    return false;
  }
  return (!end_.has_value() || seq <= *end_);
}

std::optional<webtransport::StreamId> SubscribeWindow::GetStreamForSequence(
    FullSequence sequence,
    MoqtForwardingPreference forwarding_preference) const {
  if (forwarding_preference == MoqtForwardingPreference::kTrack) {
    return track_stream_;
  }
  auto group_it = group_streams_.find(sequence.group);
  if (group_it == group_streams_.end()) {
    return std::nullopt;
  }
  if (forwarding_preference == MoqtForwardingPreference::kGroup) {
    return group_it->second.group_stream;
  }
  auto object_it = group_it->second.object_streams.find(sequence.object);
  if (object_it == group_it->second.object_streams.end()) {
    return std::nullopt;
  }
  return object_it->second;
}

void SubscribeWindow::AddStream(MoqtForwardingPreference forwarding_preference,
                                uint64_t group_id, uint64_t object_id,
                                webtransport::StreamId stream_id) {
  if (forwarding_preference == MoqtForwardingPreference::kTrack) {
    QUIC_BUG_IF(quic_bug_moqt_draft_02_01, track_stream_.has_value())
        << "Track stream already assigned";
    track_stream_ = stream_id;
    return;
  }
  if (forwarding_preference == MoqtForwardingPreference::kGroup) {
    QUIC_BUG_IF(quic_bug_moqt_draft_02_02,
                group_streams_[group_id].group_stream.has_value())
        << "Group stream already assigned";
    group_streams_[group_id].group_stream = stream_id;
    return;
  }
  // ObjectStream or ObjectPreferDatagram
  QUIC_BUG_IF(quic_bug_moqt_draft_02_03,
              group_streams_[group_id].object_streams.contains(object_id))
      << "Object stream already assigned";
  group_streams_[group_id].object_streams[object_id] = stream_id;
}

void SubscribeWindow::RemoveStream(
    MoqtForwardingPreference forwarding_preference, uint64_t group_id,
    uint64_t object_id) {
  if (forwarding_preference == moqt::MoqtForwardingPreference::kTrack) {
    track_stream_ = std::nullopt;
    return;
  }
  auto group_it = group_streams_.find(group_id);
  if (group_it == group_streams_.end()) {
    return;
  }
  GroupStreams& group = group_it->second;
  if (forwarding_preference == moqt::MoqtForwardingPreference::kGroup) {
    group.group_stream = std::nullopt;
    if (group.object_streams.empty()) {
      group_streams_.erase(group_id);
    }
    return;
  }
  // ObjectStream or ObjectPreferDatagram
  if (group.object_streams.contains(object_id)) {
    group_streams_[group_id].object_streams.erase(object_id);
    if (!group.group_stream.has_value() &&
        group_streams_[group_id].object_streams.empty()) {
      group_streams_.erase(group_id);
    }
  }
}

std::vector<SubscribeWindow*> MoqtSubscribeWindows::SequenceIsSubscribed(
    FullSequence sequence) {
  std::vector<SubscribeWindow*> retval;
  for (auto& [subscribe_id, window] : windows_) {
    if (window.InWindow(sequence)) {
      retval.push_back(&(window));
    }
  }
  return retval;
}

}  // namespace moqt
