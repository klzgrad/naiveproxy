// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_subscribe_windows.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "quiche/quic/core/quic_interval.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

std::optional<webtransport::StreamId> SendStreamMap::GetStreamFor(
    DataStreamIndex index) const {
  auto it = send_streams_.find(index);
  if (it == send_streams_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void SendStreamMap::AddStream(DataStreamIndex index,
                              webtransport::StreamId stream_id) {
  auto [it, success] = send_streams_.emplace(index, stream_id);
  QUIC_BUG_IF(quic_bug_moqt_draft_03_02, !success) << "Stream already added";
}

void SendStreamMap::RemoveStream(DataStreamIndex index) {
  send_streams_.erase(index);
}

bool SubscribeWindow::TruncateStart(Location start) {
  if (start < start_) {
    return false;
  }
  start_ = start;
  return true;
}

bool SubscribeWindow::TruncateEnd(uint64_t end_group) {
  if (end_group > end_.group) {
    return false;
  }
  end_ = Location(end_group, UINT64_MAX);
  return true;
}

bool SubscribeWindow::TruncateEnd(Location largest_id) {
  if (largest_id > end_) {
    return false;
  }
  end_ = largest_id;
  return true;
}

std::vector<webtransport::StreamId> SendStreamMap::GetAllStreams() const {
  std::vector<webtransport::StreamId> ids;
  for (const auto& [index, stream_id] : send_streams_) {
    ids.push_back(stream_id);
  }
  return ids;
}

std::vector<webtransport::StreamId> SendStreamMap::GetStreamsForGroup(
    uint64_t group_id) const {
  const auto start_it = send_streams_.lower_bound(DataStreamIndex(group_id, 0));
  const auto end_it = send_streams_.upper_bound(
      DataStreamIndex(group_id, std::numeric_limits<uint64_t>::max()));
  std::vector<webtransport::StreamId> ids;
  for (auto it = start_it; it != end_it; ++it) {
    ids.push_back(it->second);
  }
  return ids;
}

bool SubscribeWindow::GroupInWindow(uint64_t group) const {
  const quic::QuicInterval<Location> group_window(
      Location(group, 0),
      Location(group, std::numeric_limits<uint64_t>::max()));
  const quic::QuicInterval<Location> subscription_window(start_, end_);
  return group_window.Intersects(subscription_window);
}

}  // namespace moqt
