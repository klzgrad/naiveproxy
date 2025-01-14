// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_subscribe_windows.h"

#include <optional>
#include <vector>

#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

bool SubscribeWindow::InWindow(const FullSequence& seq) const {
  if (seq < start_) {
    return false;
  }
  return (!end_.has_value() || seq <= *end_);
}

std::optional<webtransport::StreamId> SendStreamMap::GetStreamForSequence(
    FullSequence sequence) const {
  ReducedSequenceIndex index(sequence, forwarding_preference_);
  auto stream_it = send_streams_.find(index);
  if (stream_it == send_streams_.end()) {
    return std::nullopt;
  }
  return stream_it->second;
}

void SendStreamMap::AddStream(FullSequence sequence,
                              webtransport::StreamId stream_id) {
  ReducedSequenceIndex index(sequence, forwarding_preference_);
  if (forwarding_preference_ == MoqtForwardingPreference::kDatagram) {
    QUIC_BUG(quic_bug_moqt_draft_03_01) << "Adding a stream for datagram";
    return;
  }
  auto [stream_it, success] = send_streams_.emplace(index, stream_id);
  QUIC_BUG_IF(quic_bug_moqt_draft_03_02, !success) << "Stream already added";
}

void SendStreamMap::RemoveStream(FullSequence sequence,
                                 webtransport::StreamId stream_id) {
  ReducedSequenceIndex index(sequence, forwarding_preference_);
  QUICHE_DCHECK(send_streams_.contains(index) &&
                send_streams_.find(index)->second == stream_id)
      << "Requested to remove a stream ID that does not match the one in the "
         "map";
  send_streams_.erase(index);
}

bool SubscribeWindow::UpdateStartEnd(FullSequence start,
                                     std::optional<FullSequence> end) {
  // Can't make the subscription window bigger.
  if (!InWindow(start)) {
    return false;
  }
  if (end_.has_value() && (!end.has_value() || *end_ < *end)) {
    return false;
  }
  start_ = start;
  end_ = end;
  return true;
}

ReducedSequenceIndex::ReducedSequenceIndex(
    FullSequence sequence, MoqtForwardingPreference preference) {
  switch (preference) {
    case MoqtForwardingPreference::kTrack:
      sequence_ = FullSequence(0, 0);
      break;
    case MoqtForwardingPreference::kSubgroup:
      sequence_ = FullSequence(sequence.group, 0);
      break;
    case MoqtForwardingPreference::kDatagram:
      sequence_ = sequence;
      return;
  }
}

std::vector<webtransport::StreamId> SendStreamMap::GetAllStreams() const {
  std::vector<webtransport::StreamId> ids;
  for (const auto& [index, id] : send_streams_) {
    ids.push_back(id);
  }
  return ids;
}

}  // namespace moqt
