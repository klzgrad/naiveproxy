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
    FullSequence sequence) const {
  FullSequence index = SequenceToIndex(sequence);
  auto stream_it = send_streams_.find(index);
  if (stream_it == send_streams_.end()) {
    return std::nullopt;
  }
  return stream_it->second;
}

void SubscribeWindow::AddStream(uint64_t group_id, uint64_t object_id,
                                webtransport::StreamId stream_id) {
  if (!InWindow(FullSequence(group_id, object_id))) {
    return;
  }
  FullSequence index = SequenceToIndex(FullSequence(group_id, object_id));
  if (forwarding_preference_ == MoqtForwardingPreference::kDatagram) {
    QUIC_BUG(quic_bug_moqt_draft_03_01) << "Adding a stream for datagram";
    return;
  }
  auto stream_it = send_streams_.find(index);
  if (stream_it != send_streams_.end()) {
    QUIC_BUG(quic_bug_moqt_draft_03_02) << "Stream already added";
    return;
  }
  send_streams_[index] = stream_id;
}

void SubscribeWindow::RemoveStream(uint64_t group_id, uint64_t object_id) {
  FullSequence index = SequenceToIndex(FullSequence(group_id, object_id));
  send_streams_.erase(index);
}

FullSequence SubscribeWindow::SequenceToIndex(FullSequence sequence) const {
  switch (forwarding_preference_) {
    case MoqtForwardingPreference::kTrack:
      return FullSequence(0, 0);
    case MoqtForwardingPreference::kGroup:
      return FullSequence(sequence.group, 0);
    case MoqtForwardingPreference::kObject:
      return sequence;
    case MoqtForwardingPreference::kDatagram:
      QUIC_BUG(quic_bug_moqt_draft_03_01) << "No stream for datagram";
      return FullSequence(0, 0);
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
