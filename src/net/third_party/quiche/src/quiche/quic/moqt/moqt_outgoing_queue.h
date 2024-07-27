// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_OUTGOING_QUEUE_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOQT_OUTGOING_QUEUE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_subscribe_windows.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace moqt {

// MoqtOutgoingQueue lets the user send objects by providing the contents of the
// object and a keyframe flag.  The queue will automatically number objects and
// groups, and maintain a buffer of three most recent groups that will be
// provided to subscribers automatically.
//
// This class is primarily meant to be used by publishers to buffer the frames
// that they produce. Limitations of this class:
// - It currently only works with the forwarding preference of kGroup.
// - It only supports a single session.
// - Everything is sent in order that it is queued.
class MoqtOutgoingQueue : public LocalTrack::Visitor {
 public:
  explicit MoqtOutgoingQueue(MoqtSession* session, FullTrackName track)
      : session_(session), track_(std::move(track)) {}

  MoqtOutgoingQueue(const MoqtOutgoingQueue&) = delete;
  MoqtOutgoingQueue(MoqtOutgoingQueue&&) = default;
  MoqtOutgoingQueue& operator=(const MoqtOutgoingQueue&) = delete;
  MoqtOutgoingQueue& operator=(MoqtOutgoingQueue&&) = default;

  // If `key` is true, the object is placed into a new group, and the previous
  // group is closed. The first object ever sent MUST have `key` set to true.
  void AddObject(quiche::QuicheMemSlice payload, bool key);

  // LocalTrack::Visitor implementation.
  absl::StatusOr<PublishPastObjectsCallback> OnSubscribeForPast(
      const SubscribeWindow& window) override;

 protected:
  // Interface to MoqtSession; can be mocked out for tests.
  virtual void CloseStreamForGroup(uint64_t group_id);
  virtual void PublishObject(uint64_t group_id, uint64_t object_id,
                             absl::string_view payload, bool close_stream);

 private:
  // The number of recent groups to keep around for newly joined subscribers.
  static constexpr size_t kMaxQueuedGroups = 3;

  using Object = quiche::QuicheMemSlice;
  using Group = std::vector<Object>;

  // The number of the oldest group available.
  uint64_t first_group_in_queue() {
    return current_group_id_ - queue_.size() + 1;
  }

  MoqtSession* session_;  // Not owned.
  FullTrackName track_;
  absl::InlinedVector<Group, kMaxQueuedGroups> queue_;
  uint64_t current_group_id_ = -1;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_OUTGOING_QUEUE_H_
