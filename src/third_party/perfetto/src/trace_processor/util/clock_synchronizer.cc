/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/util/clock_synchronizer.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"

namespace perfetto::trace_processor {

// --- ClockId ---

std::string ClockId::ToString() const {
  if (seq_id == 0 && trace_file_id == 0)
    return std::to_string(clock_id);
  return std::to_string(clock_id) + "(seq=" + std::to_string(seq_id) +
         ",tf=" + std::to_string(trace_file_id) + ")";
}

// --- ClockSynchronizerListener ---

ClockSynchronizerListener::~ClockSynchronizerListener() = default;

// --- ClockSynchronizer ---

ClockSynchronizer::ClockSynchronizer(
    TraceTimeState* trace_time_state,
    std::unique_ptr<ClockSynchronizerListener> clock_event_listener)
    : trace_time_state_(trace_time_state),
      clock_event_listener_(std::move(clock_event_listener)) {}

base::StatusOr<uint32_t> ClockSynchronizer::AddSnapshot(
    const std::vector<ClockTimestamp>& clock_timestamps) {
  const auto snapshot_id = cur_snapshot_id_++;

  // Clear the cache
  cache_.fill({});

  // Compute the fingerprint of the snapshot by hashing all clock ids. This is
  // used by the clock pathfinding logic.
  base::MurmurHashCombiner hasher;
  for (const auto& clock_ts : clock_timestamps)
    hasher.Combine(clock_ts.clock.id);
  const auto snapshot_hash = static_cast<SnapshotHash>(hasher.digest());

  // Add a new entry in each clock's snapshot vector.
  for (const auto& clock_ts : clock_timestamps) {
    ClockId clock_id = clock_ts.clock.id;
    ClockDomain& domain = clocks_[clock_id];

    if (domain.snapshots.empty()) {
      if (clock_ts.clock.is_incremental &&
          !IsConvertedSequenceClock(clock_id)) {
        clock_event_listener_->OnInvalidClockSnapshot();
        return base::ErrStatus(
            "Clock sync error: the global clock with id=%s"
            " cannot use incremental encoding; this is only "
            "supported for sequence-scoped clocks.",
            clock_id.ToString().c_str());
      }
      domain.unit_multiplier_ns = clock_ts.clock.unit_multiplier_ns;
      domain.is_incremental = clock_ts.clock.is_incremental;
    } else if (PERFETTO_UNLIKELY(domain.unit_multiplier_ns !=
                                     clock_ts.clock.unit_multiplier_ns ||
                                 domain.is_incremental !=
                                     clock_ts.clock.is_incremental)) {
      clock_event_listener_->OnInvalidClockSnapshot();
      return base::ErrStatus(
          "Clock sync error: the clock domain with id=%s"
          " (unit=%" PRId64
          ", incremental=%d), was previously registered with "
          "different properties (unit=%" PRId64 ", incremental=%d).",
          clock_id.ToString().c_str(), clock_ts.clock.unit_multiplier_ns,
          clock_ts.clock.is_incremental, domain.unit_multiplier_ns,
          domain.is_incremental);
    }
    if (PERFETTO_UNLIKELY(clock_id == trace_time_state_->clock_id &&
                          domain.unit_multiplier_ns != 1)) {
      clock_event_listener_->OnInvalidClockSnapshot();
      return base::ErrStatus(
          "Clock sync error: the trace clock (id=%s"
          ") must always use nanoseconds as unit multiplier.",
          clock_id.ToString().c_str());
    }
    const int64_t timestamp_ns = clock_ts.timestamp * domain.unit_multiplier_ns;
    domain.last_timestamp_ns = timestamp_ns;

    ClockSnapshots& vect = domain.snapshots[snapshot_hash];
    if (!vect.snapshot_ids.empty() &&
        PERFETTO_UNLIKELY(vect.snapshot_ids.back() == snapshot_id)) {
      clock_event_listener_->OnInvalidClockSnapshot();
      return base::ErrStatus(
          "Clock sync error: duplicate clock domain with id=%s"
          " at snapshot %" PRIu32 ".",
          clock_id.ToString().c_str(), snapshot_id);
    }

    // Clock ids in the range [64, 128) are sequence-scoped and must be
    // translated to global ids via SequenceToGlobalClock() before calling
    // this function.
    PERFETTO_DCHECK(!clock_id.IsSequenceClock() || clock_id.seq_id != 0);

    // Snapshot IDs must be always monotonic.
    PERFETTO_DCHECK(vect.snapshot_ids.empty() ||
                    vect.snapshot_ids.back() < snapshot_id);

    if (!vect.timestamps_ns.empty() &&
        timestamp_ns < vect.timestamps_ns.back()) {
      // Clock is not monotonic.

      if (clock_id == trace_time_state_->clock_id) {
        clock_event_listener_->OnInvalidClockSnapshot();
        return base::ErrStatus(
            "Clock sync error: the trace clock (id=%s"
            ") is not monotonic at snapshot %" PRIu32 ". %" PRId64
            " not >= %" PRId64 ".",
            clock_id.ToString().c_str(), snapshot_id, timestamp_ns,
            vect.timestamps_ns.back());
      }

      PERFETTO_DLOG("Detected non-monotonic clock with ID %s",
                    clock_id.ToString().c_str());

      // For the other clocks the best thing we can do is mark it as
      // non-monotonic and refuse to use it as a source clock in the
      // resolution graph. We can still use it as a target clock, but not
      // viceversa. The concrete example is the CLOCK_REALTIME going 1h
      // backwards during daylight saving. We can still answer the question
      // "what was the REALTIME timestamp when BOOTTIME was X?" but we can't
      // answer the opposite question because there can be two valid
      // BOOTTIME(s) for the same REALTIME instant because of the 1:many
      // relationship.
      non_monotonic_clocks_.insert(clock_id);

      // Erase all edges from the graph that start from this clock (but keep
      // the ones that end on this clock).
      PERFETTO_CHECK(clock_id.trace_file_id <
                     std::numeric_limits<uint32_t>::max());
      auto begin = graph_.lower_bound(ClockGraphEdge{clock_id, ClockId{}, 0});
      ClockId upper = {clock_id.clock_id, clock_id.seq_id,
                       clock_id.trace_file_id + 1};
      auto end = graph_.lower_bound(ClockGraphEdge{upper, ClockId{}, 0});
      graph_.erase(begin, end);
    }
    vect.snapshot_ids.emplace_back(snapshot_id);
    vect.timestamps_ns.emplace_back(timestamp_ns);
  }
  // Create graph edges for all the possible tuples of clocks in this
  // snapshot. If the snapshot contains clock a, b, c, d create edges [ab, ac,
  // ad, bc, bd, cd] and the symmetrical ones [ba, ca, da, bc, db, dc]. This
  // is to store the information: Clock A is syncable to Clock B via the
  // snapshots of type (hash).
  // Clocks that were previously marked as non-monotonic won't be added as
  // valid sources.
  for (auto it1 = clock_timestamps.begin(); it1 != clock_timestamps.end();
       ++it1) {
    auto it2 = it1;
    ++it2;
    for (; it2 != clock_timestamps.end(); ++it2) {
      if (!non_monotonic_clocks_.count(it1->clock.id))
        graph_.emplace(it1->clock.id, it2->clock.id, snapshot_hash);

      if (!non_monotonic_clocks_.count(it2->clock.id))
        graph_.emplace(it2->clock.id, it1->clock.id, snapshot_hash);
    }
  }

  return snapshot_id;
}

std::optional<int64_t> ClockSynchronizer::ConvertSlowpath(
    ClockId src_clock_id,
    int64_t src_timestamp,
    std::optional<int64_t> src_ts_ns,
    ClockId target_clock_id,
    std::optional<size_t> byte_offset) {
  PERFETTO_DCHECK(!src_clock_id.IsSequenceClock() || src_clock_id.seq_id != 0);
  PERFETTO_DCHECK(!target_clock_id.IsSequenceClock() ||
                  target_clock_id.seq_id != 0);
  clock_event_listener_->OnClockSyncCacheMiss();

  ClockPath path = FindPath(src_clock_id, target_clock_id);
  if (!path.valid()) {
    // Determine which clock(s) are unknown and record error
    ClockSyncErrorType error;
    if (clocks_.find(src_clock_id) == clocks_.end()) {
      error = ClockSyncErrorType::kUnknownSourceClock;
    } else if (clocks_.find(target_clock_id) == clocks_.end()) {
      error = ClockSyncErrorType::kUnknownTargetClock;
    } else {
      error = ClockSyncErrorType::kNoPath;
    }
    clock_event_listener_->RecordConversionError(
        error, src_clock_id, target_clock_id, src_timestamp, byte_offset);
    return std::nullopt;
  }

  // Iterate trough the path found and translate timestamps onto the new clock
  // domain on each step, until the target domain is reached.
  ClockDomain* src_domain = GetClock(src_clock_id);
  int64_t ns = src_ts_ns ? *src_ts_ns : src_domain->ToNs(src_timestamp);

  // These will track the overall translation and valid range for the whole
  // path.
  int64_t total_translation_ns = 0;
  int64_t path_min_ts_ns = std::numeric_limits<int64_t>::min();
  int64_t path_max_ts_ns = std::numeric_limits<int64_t>::max();

  for (uint32_t i = 0; i < path.len; ++i) {
    const ClockGraphEdge edge = path.at(i);
    ClockDomain* cur_clock = GetClock(std::get<0>(edge));
    ClockDomain* next_clock = GetClock(std::get<1>(edge));
    const SnapshotHash hash = std::get<2>(edge);

    // Find the closest timestamp within the snapshots of the source clock.
    const ClockSnapshots& cur_snap = cur_clock->GetSnapshot(hash);
    const auto& ts_vec = cur_snap.timestamps_ns;
    auto it = std::upper_bound(ts_vec.begin(), ts_vec.end(), ns);
    if (it != ts_vec.begin())
      --it;

    // Now lookup the snapshot id that matches the closest timestamp.
    size_t index = static_cast<size_t>(std::distance(ts_vec.begin(), it));
    PERFETTO_DCHECK(index < ts_vec.size());
    PERFETTO_DCHECK(cur_snap.snapshot_ids.size() == ts_vec.size());
    uint32_t snapshot_id = cur_snap.snapshot_ids[index];

    // And use that to retrieve the corresponding time in the next clock
    // domain. The snapshot id must exist in the target clock domain. If it
    // doesn't either the hash logic or the pathfinding logic are bugged. This
    // can also happen if the checks in AddSnapshot fail and we skip part of
    // the snapshot.
    const ClockSnapshots& next_snap = next_clock->GetSnapshot(hash);

    // Using std::lower_bound because snapshot_ids is sorted, so we can do
    // a binary search. std::find would do a linear scan.
    auto next_it = std::lower_bound(next_snap.snapshot_ids.begin(),
                                    next_snap.snapshot_ids.end(), snapshot_id);
    if (next_it == next_snap.snapshot_ids.end() || *next_it != snapshot_id) {
      PERFETTO_DFATAL("Snapshot does not exist in clock domain.");
      continue;
    }
    size_t next_index = static_cast<size_t>(
        std::distance(next_snap.snapshot_ids.begin(), next_it));
    PERFETTO_DCHECK(next_index < next_snap.snapshot_ids.size());
    int64_t next_timestamp_ns = next_snap.timestamps_ns[next_index];

    // The translated timestamp is the relative delta of the source timestamp
    // from the closest snapshot found (ns - *it), plus the timestamp in
    // the new clock domain for the same snapshot id.
    const int64_t hop_translation_ns = next_timestamp_ns - *it;
    ns += hop_translation_ns;

    // Now, calculate the valid range for this specific hop and intersect it
    // with the accumulated valid range for the whole path.
    // The range for this hop needs to be translated back to the source
    // clock's coordinate system.
    const int64_t kInt64Min = std::numeric_limits<int64_t>::min();
    const int64_t kInt64Max = std::numeric_limits<int64_t>::max();

    int64_t hop_min_ts_ns = (it == ts_vec.begin()) ? kInt64Min : *it;
    auto ubound = it + 1;
    int64_t hop_max_ts_ns = (ubound == ts_vec.end()) ? kInt64Max : *ubound;

    // Translate the hop's valid range back to the original source clock's
    // domain. `total_translation_ns` is the translation from the *start* of
    // the path to the *start* of the current hop.
    int64_t hop_min_in_src_domain_ns =
        (hop_min_ts_ns == kInt64Min) ? kInt64Min
                                     : hop_min_ts_ns - total_translation_ns;
    int64_t hop_max_in_src_domain_ns =
        (hop_max_ts_ns == kInt64Max) ? kInt64Max
                                     : hop_max_ts_ns - total_translation_ns;

    // Intersect with the path's current valid range.
    path_min_ts_ns = std::max(path_min_ts_ns, hop_min_in_src_domain_ns);
    path_max_ts_ns = std::min(path_max_ts_ns, hop_max_in_src_domain_ns);

    // Accumulate the translation.
    total_translation_ns += hop_translation_ns;

    // The last clock in the path must be the target clock.
    PERFETTO_DCHECK(i < path.len - 1 || std::get<1>(edge) == target_clock_id);
  }

  // After the loop, we have the final converted timestamp `ns`, and the
  // total translation and valid range for the entire path.
  // We can now cache this result.
  CachedClockPath cache_entry{};
  cache_entry.src = src_clock_id;
  cache_entry.target = target_clock_id;
  cache_entry.src_domain = src_domain;
  cache_entry.min_ts_ns = path_min_ts_ns;
  cache_entry.max_ts_ns = path_max_ts_ns;
  cache_entry.translation_ns = total_translation_ns;
  cache_[rnd_() % cache_.size()] = cache_entry;

  return ns;
}

ClockSynchronizer::ClockPath ClockSynchronizer::FindPath(ClockId src,
                                                         ClockId target) {
  PERFETTO_CHECK(src != target);

  // If we've never heard of the clock before there is no hope:
  if (clocks_.find(target) == clocks_.end()) {
    return ClockPath();
  }
  if (clocks_.find(src) == clocks_.end()) {
    return ClockPath();
  }

  // This is a classic breadth-first search. Each node in the queue holds also
  // the full path to reach that node.
  // We assume the graph is acyclic, if it isn't the ClockPath::kMaxLen will
  // stop the search anyways.
  queue_find_path_cache_.clear();
  queue_find_path_cache_.emplace_back(src);

  while (!queue_find_path_cache_.empty()) {
    ClockPath cur_path = queue_find_path_cache_.front();
    queue_find_path_cache_.pop_front();

    const ClockId cur_clock_id = cur_path.last;
    if (cur_path.len >= ClockPath::kMaxLen)
      continue;

    // Expore all the adjacent clocks.
    // The lower_bound() below returns an iterator to the first edge that
    // starts on |cur_clock_id|. The edges are sorted by (src, target, hash).
    for (auto it =
             graph_.lower_bound(ClockGraphEdge(cur_clock_id, ClockId{}, 0));
         it != graph_.end() && std::get<0>(*it) == cur_clock_id; ++it) {
      ClockId next_clock_id = std::get<1>(*it);
      SnapshotHash hash = std::get<2>(*it);
      if (next_clock_id == target)
        return ClockPath(cur_path, next_clock_id, hash);
      queue_find_path_cache_.emplace_back(
          ClockPath(cur_path, next_clock_id, hash));
    }
  }
  return ClockPath();  // invalid path.
}

ClockSynchronizer::ClockDomain* ClockSynchronizer::GetClock(ClockId clock_id) {
  auto it = clocks_.find(clock_id);
  PERFETTO_DCHECK(it != clocks_.end());
  return &it->second;
}

}  // namespace perfetto::trace_processor
