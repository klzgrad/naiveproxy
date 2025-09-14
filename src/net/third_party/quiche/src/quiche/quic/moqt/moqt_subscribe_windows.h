// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H
#define QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/container/btree_map.h"
#include "quiche/quic/core/quic_interval.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_publisher.h"
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
  bool GroupInWindow(uint64_t group) const;
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

// A tuple uniquely identifying a WebTransport data stream associated with a
// subscription. By convention, if a DataStreamIndex is necessary for a datagram
// track, `subgroup` is set to zero.
struct DataStreamIndex {
  uint64_t group = 0;
  uint64_t subgroup = 0;

  DataStreamIndex() = default;
  DataStreamIndex(uint64_t group, uint64_t subgroup)
      : group(group), subgroup(subgroup) {}
  explicit DataStreamIndex(const PublishedObject& object)
      : group(object.metadata.location.group),
        subgroup(object.metadata.subgroup) {}

  auto operator<=>(const DataStreamIndex&) const = default;

  template <typename H>
  friend H AbslHashValue(H h, const DataStreamIndex& index) {
    return H::combine(std::move(h), index.group, index.subgroup);
  }
};

// A map of outgoing data streams indexed by object sequence numbers.
class QUICHE_EXPORT SendStreamMap {
 public:
  SendStreamMap() = default;

  std::optional<webtransport::StreamId> GetStreamFor(
      DataStreamIndex index) const;
  void AddStream(DataStreamIndex index, webtransport::StreamId stream_id);
  void RemoveStream(DataStreamIndex index);
  std::vector<webtransport::StreamId> GetAllStreams() const;
  std::vector<webtransport::StreamId> GetStreamsForGroup(
      uint64_t group_id) const;

 private:
  absl::btree_map<DataStreamIndex, webtransport::StreamId> send_streams_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H
