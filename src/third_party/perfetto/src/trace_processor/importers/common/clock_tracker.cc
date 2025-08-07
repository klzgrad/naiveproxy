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

#include "src/trace_processor/importers/common/clock_tracker.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <optional>
#include <queue>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"

namespace perfetto::trace_processor {

using Clock = protos::pbzero::ClockSnapshot::Clock;

ClockTracker::ClockTracker(TraceProcessorContext* context)
    : context_(context),
      trace_time_clock_id_(protos::pbzero::BUILTIN_CLOCK_BOOTTIME) {}

ClockTracker::~ClockTracker() = default;

base::StatusOr<uint32_t> ClockTracker::AddSnapshot(
    const std::vector<ClockTimestamp>& clock_timestamps) {
  const auto snapshot_id = cur_snapshot_id_++;

  // Clear the cache
  cache_.fill({});

  // Compute the fingerprint of the snapshot by hashing all clock ids. This is
  // used by the clock pathfinding logic.
  base::Hasher hasher;
  for (const auto& clock_ts : clock_timestamps)
    hasher.Update(clock_ts.clock.id);
  const auto snapshot_hash = static_cast<SnapshotHash>(hasher.digest());

  // Add a new entry in each clock's snapshot vector.
  for (const auto& clock_ts : clock_timestamps) {
    ClockId clock_id = clock_ts.clock.id;
    ClockDomain& domain = clocks_[clock_id];

    if (domain.snapshots.empty()) {
      if (clock_ts.clock.is_incremental &&
          !IsConvertedSequenceClock(clock_id)) {
        context_->storage->IncrementStats(stats::invalid_clock_snapshots);
        return base::ErrStatus(
            "Clock sync error: the global clock with id=%" PRId64
            " cannot use incremental encoding; this is only "
            "supported for sequence-scoped clocks.",
            clock_id);
      }
      domain.unit_multiplier_ns = clock_ts.clock.unit_multiplier_ns;
      domain.is_incremental = clock_ts.clock.is_incremental;
    } else if (PERFETTO_UNLIKELY(domain.unit_multiplier_ns !=
                                     clock_ts.clock.unit_multiplier_ns ||
                                 domain.is_incremental !=
                                     clock_ts.clock.is_incremental)) {
      context_->storage->IncrementStats(stats::invalid_clock_snapshots);
      return base::ErrStatus(
          "Clock sync error: the clock domain with id=%" PRId64
          " (unit=%" PRId64
          ", incremental=%d), was previously registered with "
          "different properties (unit=%" PRId64 ", incremental=%d).",
          clock_id, clock_ts.clock.unit_multiplier_ns,
          clock_ts.clock.is_incremental, domain.unit_multiplier_ns,
          domain.is_incremental);
    }
    if (PERFETTO_UNLIKELY(clock_id == trace_time_clock_id_ &&
                          domain.unit_multiplier_ns != 1)) {
      // The trace time clock must always be in nanoseconds.
      context_->storage->IncrementStats(stats::invalid_clock_snapshots);
      return base::ErrStatus(
          "Clock sync error: the trace clock (id=%" PRId64
          ") must always use nanoseconds as unit multiplier.",
          clock_id);
    }
    const int64_t timestamp_ns = clock_ts.timestamp * domain.unit_multiplier_ns;
    domain.last_timestamp_ns = timestamp_ns;

    ClockSnapshots& vect = domain.snapshots[snapshot_hash];
    if (!vect.snapshot_ids.empty() &&
        PERFETTO_UNLIKELY(vect.snapshot_ids.back() == snapshot_id)) {
      context_->storage->IncrementStats(stats::invalid_clock_snapshots);
      return base::ErrStatus(
          "Clock sync error: duplicate clock domain with id=%" PRId64
          " at snapshot %" PRIu32 ".",
          clock_id, snapshot_id);
    }

    // Clock ids in the range [64, 128) are sequence-scoped and must be
    // translated to global ids via SeqScopedClockIdToGlobal() before calling
    // this function.
    PERFETTO_DCHECK(!IsSequenceClock(clock_id));

    // Snapshot IDs must be always monotonic.
    PERFETTO_DCHECK(vect.snapshot_ids.empty() ||
                    vect.snapshot_ids.back() < snapshot_id);

    if (!vect.timestamps_ns.empty() &&
        timestamp_ns < vect.timestamps_ns.back()) {
      // Clock is not monotonic.

      if (clock_id == trace_time_clock_id_) {
        context_->storage->IncrementStats(stats::invalid_clock_snapshots);
        // The trace clock cannot be non-monotonic.
        return base::ErrStatus("Clock sync error: the trace clock (id=%" PRId64
                               ") is not monotonic at snapshot %" PRIu32
                               ". %" PRId64 " not >= %" PRId64 ".",
                               clock_id, snapshot_id, timestamp_ns,
                               vect.timestamps_ns.back());
      }

      PERFETTO_DLOG("Detected non-monotonic clock with ID %" PRId64, clock_id);

      // For the other clocks the best thing we can do is mark it as
      // non-monotonic and refuse to use it as a source clock in the resolution
      // graph. We can still use it as a target clock, but not viceversa.
      // The concrete example is the CLOCK_REALTIME going 1h backwards during
      // daylight saving. We can still answer the question "what was the
      // REALTIME timestamp when BOOTTIME was X?" but we can't answer the
      // opposite question because there can be two valid BOOTTIME(s) for the
      // same REALTIME instant because of the 1:many relationship.
      non_monotonic_clocks_.insert(clock_id);

      // Erase all edges from the graph that start from this clock (but keep the
      // ones that end on this clock).
      auto begin = graph_.lower_bound(ClockGraphEdge{clock_id, 0, 0});
      auto end = graph_.lower_bound(ClockGraphEdge{clock_id + 1, 0, 0});
      graph_.erase(begin, end);
    }
    vect.snapshot_ids.emplace_back(snapshot_id);
    vect.timestamps_ns.emplace_back(timestamp_ns);
  }

  // Create graph edges for all the possible tuples of clocks in this snapshot.
  // If the snapshot contains clock a, b, c, d create edges [ab, ac, ad, bc, bd,
  // cd] and the symmetrical ones [ba, ca, da, bc, db, dc].
  // This is to store the information: Clock A is syncable to Clock B via the
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

// Finds the shortest clock resolution path in the graph that allows to
// translate a timestamp from |src| to |target| clocks.
// The return value looks like the following: "If you want to convert a
// timestamp from clock C1 to C2 you need to first convert C1 -> C3 using the
// snapshot hash A, then convert C3 -> C2 via snapshot hash B".
ClockTracker::ClockPath ClockTracker::FindPath(ClockId src, ClockId target) {
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
  std::queue<ClockPath> queue;
  queue.emplace(src);

  while (!queue.empty()) {
    ClockPath cur_path = queue.front();
    queue.pop();

    const ClockId cur_clock_id = cur_path.last;
    if (cur_clock_id == target)
      return cur_path;

    if (cur_path.len >= ClockPath::kMaxLen)
      continue;

    // Expore all the adjacent clocks.
    // The lower_bound() below returns an iterator to the first edge that starts
    // on |cur_clock_id|. The edges are sorted by (src, target, hash).
    for (auto it = graph_.lower_bound(ClockGraphEdge(cur_clock_id, 0, 0));
         it != graph_.end() && std::get<0>(*it) == cur_clock_id; ++it) {
      ClockId next_clock_id = std::get<1>(*it);
      SnapshotHash hash = std::get<2>(*it);
      queue.push(ClockPath(cur_path, next_clock_id, hash));
    }
  }
  return ClockPath();  // invalid path.
}

std::optional<int64_t> ClockTracker::ToTraceTimeFromSnapshot(
    const std::vector<ClockTimestamp>& snapshot) {
  auto maybe_found_trace_time_clock = std::find_if(
      snapshot.begin(), snapshot.end(),
      [this](const ClockTimestamp& clock_timestamp) {
        return clock_timestamp.clock.id == this->trace_time_clock_id_;
      });

  if (maybe_found_trace_time_clock == snapshot.end())
    return std::nullopt;

  return maybe_found_trace_time_clock->timestamp;
}

base::StatusOr<int64_t> ClockTracker::ConvertSlowpath(ClockId src_clock_id,
                                                      int64_t src_timestamp,
                                                      ClockId target_clock_id) {
  PERFETTO_DCHECK(!IsSequenceClock(src_clock_id));
  PERFETTO_DCHECK(!IsSequenceClock(target_clock_id));

  context_->storage->IncrementStats(stats::clock_sync_cache_miss);

  ClockPath path = FindPath(src_clock_id, target_clock_id);

  if (!path.valid()) {
    // Too many logs maybe emitted when path is invalid.
    context_->storage->IncrementStats(stats::clock_sync_failure);
    return base::ErrStatus("No path from clock %" PRId64 " to %" PRId64
                           " at timestamp %" PRId64,
                           src_clock_id, target_clock_id, src_timestamp);
  }

  // We can cache only single-path resolutions between two clocks.
  // Caching multi-path resolutions is harder because the (src,target) tuple
  // is not enough as a cache key: at any step the |ns| value can yield to a
  // different choice of the next snapshot. Multi-path resolutions don't seem
  // too frequent these days, so we focus only on caching the more frequent
  // one-step resolutions (typically from any clock to the trace clock).
  const bool cacheable = path.len == 1;
  CachedClockPath cache_entry{};

  // Iterate trough the path found and translate timestamps onto the new clock
  // domain on each step, until the target domain is reached.
  ClockDomain* src_domain = GetClock(src_clock_id);
  int64_t ns = src_domain->ToNs(src_timestamp);
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

    // And use that to retrieve the corresponding time in the next clock domain.
    // The snapshot id must exist in the target clock domain. If it doesn't
    // either the hash logic or the pathfinding logic are bugged.
    // This can also happen if the checks in AddSnapshot fail and we skip part
    // of the snapshot.
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
    const int64_t adj = next_timestamp_ns - *it;
    ns += adj;

    // On the first iteration, keep track of the bounds for the cache entry.
    // This will allow future Convert() calls to skip the pathfinder logic
    // as long as the query stays within the bound.
    if (cacheable) {
      PERFETTO_DCHECK(i == 0);
      const int64_t kInt64Min = std::numeric_limits<int64_t>::min();
      const int64_t kInt64Max = std::numeric_limits<int64_t>::max();
      cache_entry.min_ts_ns = it == ts_vec.begin() ? kInt64Min : *it;
      auto ubound = it + 1;
      cache_entry.max_ts_ns = ubound == ts_vec.end() ? kInt64Max : *ubound;
      cache_entry.translation_ns = adj;
    }

    // The last clock in the path must be the target clock.
    PERFETTO_DCHECK(i < path.len - 1 || std::get<1>(edge) == target_clock_id);
  }

  if (cacheable) {
    cache_entry.src = src_clock_id;
    cache_entry.src_domain = src_domain;
    cache_entry.target = target_clock_id;
    cache_[rnd_() % cache_.size()] = cache_entry;
  }

  return ns;
}

}  // namespace perfetto::trace_processor
