// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H
#define QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/container/btree_map.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// SubscribeWindow represents a window of objects for which an MoQT subscription
// can be valid.
class QUICHE_EXPORT SubscribeWindow {
 public:
  // Creates a half-open window for SUBSCRIBES.
  SubscribeWindow() = default;
  SubscribeWindow(Location start) : start_(start) {}

  // Creates a closed window for SUBSCRIBE or FETCH with no end object;
  SubscribeWindow(Location start, std::optional<uint64_t> end_group)
      : start_(start),
        end_(Location(end_group.value_or(UINT64_MAX), UINT64_MAX)) {}
  // For FETCH with end object
  SubscribeWindow(Location start, uint64_t end_group,
                  std::optional<uint64_t> end_object)
      : start_(start),
        end_(Location(end_group, end_object.value_or(UINT64_MAX))) {}

  bool InWindow(const Location& seq) const {
    return start_ <= seq && seq <= end_;
  }
  Location start() const { return start_; }
  Location end() const { return end_; }

  // Updates the subscription window. Returns true if the update is valid (in
  // MoQT, subscription windows are only allowed to shrink, not to expand).
  // Called only as a result of SUBSCRIBE_OK (largest_id) or SUBSCRIBE_UPDATE.
  bool TruncateStart(Location start);
  // Called only as a result of SUBSCRIBE_UPDATE.
  bool TruncateEnd(uint64_t end_group);
  // Called only as a result of FETCH_OK (largest_id)
  bool TruncateEnd(Location largest_id);

 private:
  // The subgroups in these sequences have no meaning.
  Location start_ = Location();
  Location end_ = Location(UINT64_MAX, UINT64_MAX);
};

// ReducedSequenceIndex represents an index object such that if two sequence
// numbers are mapped to the same stream, they will be mapped to the same index.
class ReducedSequenceIndex {
 public:
  ReducedSequenceIndex(Location sequence, MoqtForwardingPreference preference);

  bool operator==(const ReducedSequenceIndex& other) const {
    return sequence_ == other.sequence_;
  }
  bool operator!=(const ReducedSequenceIndex& other) const {
    return sequence_ != other.sequence_;
  }
  Location sequence() { return sequence_; }

  template <typename H>
  friend H AbslHashValue(H h, const ReducedSequenceIndex& m) {
    return H::combine(std::move(h), m.sequence_);
  }

 private:
  Location sequence_;
};

// A map of outgoing data streams indexed by object sequence numbers.
class QUICHE_EXPORT SendStreamMap {
 public:
  explicit SendStreamMap(MoqtForwardingPreference forwarding_preference)
      : forwarding_preference_(forwarding_preference) {}

  std::optional<webtransport::StreamId> GetStreamForSequence(
      Location sequence) const;
  void AddStream(Location sequence, webtransport::StreamId stream_id);
  void RemoveStream(Location sequence, webtransport::StreamId stream_id);
  std::vector<webtransport::StreamId> GetAllStreams() const;
  std::vector<webtransport::StreamId> GetStreamsForGroup(
      uint64_t group_id) const;

 private:
  using Group = absl::btree_map<uint64_t, webtransport::StreamId>;
  absl::btree_map<uint64_t, Group> send_streams_;
  MoqtForwardingPreference forwarding_preference_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H
