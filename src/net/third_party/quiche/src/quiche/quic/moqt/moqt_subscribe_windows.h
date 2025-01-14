// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H
#define QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// SubscribeWindow represents a window of objects for which an MoQT subscription
// can be valid.
class QUICHE_EXPORT SubscribeWindow {
 public:
  // Creates a half-open window. |next_object| is the expected sequence number
  // of the next published object on the track.
  SubscribeWindow(uint64_t start_group, uint64_t start_object)
      : SubscribeWindow(FullSequence(start_group, start_object), std::nullopt) {
  }

  // Creates a closed window.
  SubscribeWindow(uint64_t start_group, uint64_t start_object,
                  uint64_t end_group, uint64_t end_object)
      : SubscribeWindow(FullSequence(start_group, start_object),
                        FullSequence(end_group, end_object)) {}

  SubscribeWindow(FullSequence start, std::optional<FullSequence> end)
      : start_(start), end_(end) {}

  bool InWindow(const FullSequence& seq) const;
  bool HasEnd() const { return end_.has_value(); }
  FullSequence start() const { return start_; }

  // Updates the subscription window. Returns true if the update is valid (in
  // MoQT, subscription windows are only allowed to shrink, not to expand).
  bool UpdateStartEnd(FullSequence start, std::optional<FullSequence> end);

 private:
  FullSequence start_;
  std::optional<FullSequence> end_;
};

// ReducedSequenceIndex represents an index object such that if two sequence
// numbers are mapped to the same stream, they will be mapped to the same index.
class ReducedSequenceIndex {
 public:
  ReducedSequenceIndex(FullSequence sequence,
                       MoqtForwardingPreference preference);

  bool operator==(const ReducedSequenceIndex& other) const {
    return sequence_ == other.sequence_;
  }
  bool operator!=(const ReducedSequenceIndex& other) const {
    return sequence_ != other.sequence_;
  }

  template <typename H>
  friend H AbslHashValue(H h, const ReducedSequenceIndex& m) {
    return H::combine(std::move(h), m.sequence_);
  }

 private:
  FullSequence sequence_;
};

// A map of outgoing data streams indexed by object sequence numbers.
class QUICHE_EXPORT SendStreamMap {
 public:
  explicit SendStreamMap(MoqtForwardingPreference forwarding_preference)
      : forwarding_preference_(forwarding_preference) {}

  std::optional<webtransport::StreamId> GetStreamForSequence(
      FullSequence sequence) const;
  void AddStream(FullSequence sequence, webtransport::StreamId stream_id);
  void RemoveStream(FullSequence sequence, webtransport::StreamId stream_id);
  std::vector<webtransport::StreamId> GetAllStreams() const;

 private:
  absl::flat_hash_map<ReducedSequenceIndex, webtransport::StreamId>
      send_streams_;
  MoqtForwardingPreference forwarding_preference_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H
