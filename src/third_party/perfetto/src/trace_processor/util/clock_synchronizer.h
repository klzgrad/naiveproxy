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

#ifndef SRC_TRACE_PROCESSOR_UTIL_CLOCK_SYNCHRONIZER_H_
#define SRC_TRACE_PROCESSOR_UTIL_CLOCK_SYNCHRONIZER_H_

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <tuple>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/circular_queue.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"

namespace perfetto::trace_processor {

class ClockTrackerTest;
class TraceProcessorContext;

// This class handles synchronization of timestamps across different clock
// domains. This includes multi-hop conversions from two clocks A and D, e.g.
// A->B -> B->C -> C->D, even if we never saw a snapshot that contains A and D
// at the same time.
// The API is fairly simple (but the inner operation is not):
// - AddSnapshot(map<clock_id, timestamp>): pushes a set of clocks that have
//   been snapshotted at the same time (within technical limits).
// - ToTraceTime(src_clock_id, src_timestamp):
//   converts a timestamp between clock domain and TraceTime.
//
// Concepts:
// - Snapshot hash:
//   As new snapshots are pushed via AddSnapshot() we compute a snapshot hash.
//   Such hash is the hash(clock_ids) (only IDs, not their timestamps) and is
//   used to find other snapshots that involve the same clock domains.
//   Two clock snapshots have the same hash iff they snapshot the same set of
//   clocks (the order of clocks is irrelevant).
//   This hash is used to efficiently go from the clock graph pathfinder to the
//   time-series obtained by appending the various snapshots.
// - Snapshot id:
//   A simple monotonic counter that is incremented on each AddSnapshot() call.
//
// Data structures:
//  - For each clock domain:
//    - For each snapshot hash:
//      - A logic vector of (snapshot_id, timestamp) tuples (physically stored
//        as two vectors of the same length instead of a vector of pairs).
// This allows to efficiently binary search timestamps within a clock domain
// that were obtained through a particular snapshot.
//
// - A graph of edges (source_clock, target_clock) -> snapshot hash.
//
// Operation:
// Upon each AddSnapshot() call, we incrementally build an unweighted, directed
// graph, which has clock domains as nodes.
// The graph is timestamp-oblivious. As long as we see one snapshot that
// connects two clocks, we assume we'll always be able to convert between them.
// This graph is queried by the Convert() function to figure out the shortest
// path between clock domain, possibly involving hopping through snapshots of
// different type (i.e. different hash).
//
// Example:

// We see a snapshot, with hash S1, for clocks (A,B,C). We build the edges in
// the graph: A->B, B->C, A->C (and the symmetrical ones). In other words we
// keep track of the fact that we can convert between any of them using S1.
// Later we get another snapshot containing (C,E), this snapshot will have a
// different hash (S2, because Hash(C,E) != Hash(A,B,C)) and will add the edges
// C->E, E->C [via S2] to the graph.
// At this point when we are asked to convert a timestamp from A to E, or
// viceversa, we use a simple BFS to figure out a conversion path that is:
// A->C [via S1] + C->E [via S2].
//
// Visually:
// Assume we make the following calls:
//  - AddSnapshot(A:10, B:100)
//  - AddSnapshot(A:20, C:2000)
//  - AddSnapshot(B:400, C:5000)
//  - AddSnapshot(A:30, B:300)

// And assume Hash(A,B) = S1, H(A,C) = S2, H(B,C) = S3
// The vectors in the tracker will look as follows:
// Clock A:
//   S1        {t:10, id:1}                                      {t:30, id:4}
//   S2        |               {t:20, id:2}                      |
//             |               |                                 |
// Clock B:    |               |                                 |
//   S1        {t:100, id:1}   |                                 {t:300, id:4}
//   S3                        |                  {t:400, id:3}
//                             |                  |
// Clock C:                    |                  |
//   S2                        {t: 2000, id: 2}   |
//   S3                                           {t:5000, id:3}

template <typename TClockEventListener>
class ClockSynchronizer {
 public:
  using ClockId = int64_t;

  explicit ClockSynchronizer(
      std::unique_ptr<TClockEventListener> clock_event_listener)
      : trace_time_clock_id_(protos::pbzero::BUILTIN_CLOCK_BOOTTIME),
        clock_event_listener_(std::move(clock_event_listener)) {}

  // Clock description.
  struct Clock {
    explicit Clock(ClockId clock_id) : id(clock_id) {}
    Clock(ClockId clock_id, int64_t unit, bool incremental)
        : id(clock_id), unit_multiplier_ns(unit), is_incremental(incremental) {}

    ClockId id;
    int64_t unit_multiplier_ns = 1;
    bool is_incremental = false;
  };

  // Timestamp with clock.
  struct ClockTimestamp {
    ClockTimestamp(ClockId id, int64_t ts) : clock(id), timestamp(ts) {}
    ClockTimestamp(ClockId id, int64_t ts, int64_t unit, bool incremental)
        : clock(id, unit, incremental), timestamp(ts) {}

    Clock clock;
    int64_t timestamp;
  };

  // IDs in the range [64, 128) are reserved for sequence-scoped clock ids.
  // They can't be passed directly in ClockSynchronizer calls and must be
  // resolved to 64-bit global clock ids by calling SeqScopedClockIdToGlobal().
  static bool IsSequenceClock(ClockId clock_id) {
    return clock_id >= 64 && clock_id < 128;
  }

  // Converts a sequence-scoped clock ids to a global clock id that can be
  // passed as argument to ClockSynchronizer functions.
  static ClockId SequenceToGlobalClock(uint32_t seq_id, uint32_t clock_id) {
    PERFETTO_DCHECK(IsSequenceClock(clock_id));
    return (static_cast<int64_t>(seq_id) << 32) | clock_id;
  }

  // Converts a timestamp from an arbitrary clock domain to the trace time.
  // On the first call, it also "locks" the trace time clock, preventing it
  // from being changed later.
  PERFETTO_ALWAYS_INLINE base::StatusOr<int64_t> ToTraceTime(
      ClockId clock_id,
      int64_t timestamp) {
    if (PERFETTO_UNLIKELY(!trace_time_clock_id_used_for_conversion_)) {
      clock_event_listener_->OnTraceTimeClockIdChanged(trace_time_clock_id_);
    }
    trace_time_clock_id_used_for_conversion_ = true;

    if (clock_id == trace_time_clock_id_) {
      return ToHostTraceTime(timestamp);
    }

    ASSIGN_OR_RETURN(int64_t ts,
                     Convert(clock_id, timestamp, trace_time_clock_id_));
    return ToHostTraceTime(ts);
  }

  // Appends a new snapshot for the given clock domains.
  // This is typically called by the code that reads the ClockSnapshot packet.
  // Returns the internal snapshot id of this set of clocks.
  base::StatusOr<uint32_t> AddSnapshot(
      const std::vector<ClockTimestamp>& clock_timestamps) {
    const auto snapshot_id = cur_snapshot_id_++;

    // Clear the cache
    cache_.fill({});

    // Compute the fingerprint of the snapshot by hashing all clock ids. This is
    // used by the clock pathfinding logic.
    base::FnvHasher hasher;
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
          clock_event_listener_->OnInvalidClockSnapshot();
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
        clock_event_listener_->OnInvalidClockSnapshot();
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
        clock_event_listener_->OnInvalidClockSnapshot();
        return base::ErrStatus(
            "Clock sync error: the trace clock (id=%" PRId64
            ") must always use nanoseconds as unit multiplier.",
            clock_id);
      }
      const int64_t timestamp_ns =
          clock_ts.timestamp * domain.unit_multiplier_ns;
      domain.last_timestamp_ns = timestamp_ns;

      ClockSnapshots& vect = domain.snapshots[snapshot_hash];
      if (!vect.snapshot_ids.empty() &&
          PERFETTO_UNLIKELY(vect.snapshot_ids.back() == snapshot_id)) {
        clock_event_listener_->OnInvalidClockSnapshot();
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
          clock_event_listener_->OnInvalidClockSnapshot();
          // The trace clock cannot be non-monotonic.
          return base::ErrStatus(
              "Clock sync error: the trace clock (id=%" PRId64
              ") is not monotonic at snapshot %" PRIu32 ". %" PRId64
              " not >= %" PRId64 ".",
              clock_id, snapshot_id, timestamp_ns, vect.timestamps_ns.back());
        }

        PERFETTO_DLOG("Detected non-monotonic clock with ID %" PRId64,
                      clock_id);

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
        auto begin = graph_.lower_bound(ClockGraphEdge{clock_id, 0, 0});
        auto end = graph_.lower_bound(ClockGraphEdge{clock_id + 1, 0, 0});
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

  // If trace clock and source clock are available in the snapshot will return
  // the trace clock time in snapshot.
  std::optional<int64_t> ToTraceTimeFromSnapshot(
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

  // Sets the offset for a given clock to convert timestamps from a remote
  // machine to the host's trace time. This is typically called by the code
  // that reads the RemoteClockSync packet. Typically only the offset of
  // |trace_time_clock_id_| (which is CLOCK_BOOTTIME) is used.
  void SetRemoteClockOffset(ClockId clock_id, int64_t offset) {
    remote_clock_offsets_[clock_id] = offset;
  }

  // Sets the clock domain to be used as the trace time.
  // Can be called multiple times with the same clock_id, but will log an error
  // and do nothing if called with a different clock_id after a timestamp
  // conversion has already occurred.
  base::Status SetTraceTimeClock(ClockId clock_id) {
    PERFETTO_DCHECK(!IsSequenceClock(clock_id));
    if (trace_time_clock_id_used_for_conversion_ &&
        trace_time_clock_id_ != clock_id) {
      return base::ErrStatus(
          "Not updating trace time clock from %" PRId64 " to %" PRId64
          " because the old clock was already used for timestamp "
          "conversion - ClockSnapshot too late in trace?",
          trace_time_clock_id_, clock_id);
    }
    trace_time_clock_id_ = clock_id;
    clock_event_listener_->OnSetTraceTimeClock(clock_id);

    return base::OkStatus();
  }

  // Returns the timezone offset in seconds from UTC, if one has been set.
  std::optional<int64_t> timezone_offset() const { return timezone_offset_; }

  // Sets the timezone offset in seconds from UTC.
  void set_timezone_offset(int64_t offset) { timezone_offset_ = offset; }

  // For testing:
  void set_cache_lookups_disabled_for_testing(bool v) {
    cache_lookups_disabled_for_testing_ = v;
  }

  const base::FlatHashMap<ClockId, int64_t>&
  remote_clock_offsets_for_testing() {
    return remote_clock_offsets_;
  }

  uint32_t cache_hits_for_testing() const { return cache_hits_for_testing_; }

 private:
  using SnapshotHash = uint32_t;

  // 0th argument is the source clock, 1st argument is the target clock.
  using ClockGraphEdge = std::tuple<ClockId, ClockId, SnapshotHash>;

  // TODO(b/273263113): Remove in the next stages.
  friend class ClockTrackerTest;

  // A value-type object that carries the information about the path between
  // two clock domains. It's used by the BFS algorithm.
  struct ClockPath {
    static constexpr size_t kMaxLen = 4;
    ClockPath() = default;
    ClockPath(const ClockPath&) = default;

    // Constructs an invalid path with just a source node.
    explicit ClockPath(ClockId clock_id) : last(clock_id) {}

    // Constructs a path by appending a node to |prefix|.
    // If |prefix| = [A,B] and clock_id = C, then |this| = [A,B,C].
    ClockPath(const ClockPath& prefix, ClockId clock_id, SnapshotHash hash) {
      PERFETTO_DCHECK(prefix.len < kMaxLen);
      len = prefix.len + 1;
      path = prefix.path;
      path[prefix.len] = ClockGraphEdge{prefix.last, clock_id, hash};
      last = clock_id;
    }

    bool valid() const { return len > 0; }
    const ClockGraphEdge& at(uint32_t i) const {
      PERFETTO_DCHECK(i < len);
      return path[i];
    }

    uint32_t len = 0;
    ClockId last = 0;
    std::array<ClockGraphEdge, kMaxLen> path;  // Deliberately uninitialized.
  };

  struct ClockSnapshots {
    // Invariant: both vectors have the same length.
    std::vector<uint32_t> snapshot_ids;
    std::vector<int64_t> timestamps_ns;
  };

  struct ClockDomain {
    // One time-series for each hash.
    std::map<SnapshotHash, ClockSnapshots> snapshots;

    // Multiplier for timestamps given in this domain.
    int64_t unit_multiplier_ns = 1;

    // Whether this clock domain encodes timestamps as deltas. This is only
    // supported on sequence-local domains.
    bool is_incremental = false;

    // If |is_incremental| is true, this stores the most recent absolute
    // timestamp in nanoseconds.
    int64_t last_timestamp_ns = 0;

    // Treats |timestamp| as delta timestamp if the clock uses incremental
    // encoding, and as absolute timestamp otherwise.
    int64_t ToNs(int64_t timestamp) {
      if (!is_incremental)
        return timestamp * unit_multiplier_ns;

      int64_t delta_ns = timestamp * unit_multiplier_ns;
      last_timestamp_ns += delta_ns;
      return last_timestamp_ns;
    }

    const ClockSnapshots& GetSnapshot(uint32_t hash) const {
      auto it = snapshots.find(hash);
      PERFETTO_DCHECK(it != snapshots.end());
      return it->second;
    }
  };

  // Holds data for cached entries. At the moment only single-path resolution
  // are cached.
  struct CachedClockPath {
    ClockId src;
    ClockId target;
    ClockDomain* src_domain;
    int64_t min_ts_ns;
    int64_t max_ts_ns;
    int64_t translation_ns;
  };

  ClockSynchronizer(const ClockSynchronizer&) = delete;
  ClockSynchronizer& operator=(const ClockSynchronizer&) = delete;

  base::StatusOr<int64_t> ConvertSlowpath(
      ClockId src_clock_id,
      int64_t src_timestamp,
      std::optional<int64_t> src_timestamp_ns,
      ClockId target_clock_id) {
    PERFETTO_DCHECK(!IsSequenceClock(src_clock_id));
    PERFETTO_DCHECK(!IsSequenceClock(target_clock_id));
    clock_event_listener_->OnClockSyncCacheMiss();

    ClockPath path = FindPath(src_clock_id, target_clock_id);
    if (!path.valid()) {
      // Too many logs maybe emitted when path is invalid.
      return base::ErrStatus("No path from clock %" PRId64 " to %" PRId64
                             " at timestamp %" PRId64,
                             src_clock_id, target_clock_id, src_timestamp);
    }

    // Iterate trough the path found and translate timestamps onto the new clock
    // domain on each step, until the target domain is reached.
    ClockDomain* src_domain = GetClock(src_clock_id);
    int64_t ns =
        src_timestamp_ns ? *src_timestamp_ns : src_domain->ToNs(src_timestamp);

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
      auto next_it =
          std::lower_bound(next_snap.snapshot_ids.begin(),
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

  // Converts a timestamp between two clock domains. Tries to use the cache
  // first (only for single-path resolutions), then falls back on path finding
  // as described in the header.
  base::StatusOr<int64_t> Convert(ClockId src_clock_id,
                                  int64_t src_timestamp,
                                  ClockId target_clock_id) {
    std::optional<int64_t> ns;
    if (PERFETTO_LIKELY(!cache_lookups_disabled_for_testing_)) {
      for (const auto& cached_clock_path : cache_) {
        if (cached_clock_path.src != src_clock_id ||
            cached_clock_path.target != target_clock_id) {
          continue;
        }
        if (!ns) {
          ns = cached_clock_path.src_domain->ToNs(src_timestamp);
        }
        if (*ns >= cached_clock_path.min_ts_ns &&
            *ns < cached_clock_path.max_ts_ns) {
          cache_hits_for_testing_++;
          return *ns + cached_clock_path.translation_ns;
        }
      }
    }
    return ConvertSlowpath(src_clock_id, src_timestamp, ns, target_clock_id);
  }

  // Returns whether |global_clock_id| represents a sequence-scoped clock, i.e.
  // a ClockId returned by SeqScopedClockIdToGlobal().
  static bool IsConvertedSequenceClock(ClockId global_clock_id) {
    // If the id is > 2**32, this is a sequence-scoped clock id translated into
    // the global namespace.
    return (global_clock_id >> 32) > 0;
  }

  // Finds the shortest clock resolution path in the graph that allows to
  // translate a timestamp from |src| to |target| clocks.
  // The return value looks like the following: "If you want to convert a
  // timestamp from clock C1 to C2 you need to first convert C1 -> C3 using the
  // snapshot hash A, then convert C3 -> C2 via snapshot hash B".
  ClockPath FindPath(ClockId src, ClockId target) {
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
      for (auto it = graph_.lower_bound(ClockGraphEdge(cur_clock_id, 0, 0));
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

  ClockDomain* GetClock(ClockId clock_id) {
    auto it = clocks_.find(clock_id);
    PERFETTO_DCHECK(it != clocks_.end());
    return &it->second;
  }

  // Apply the clock offset to convert remote trace times to host trace time.
  PERFETTO_ALWAYS_INLINE int64_t ToHostTraceTime(int64_t timestamp) {
    if (PERFETTO_LIKELY(clock_event_listener_->IsLocalHost())) {
      // No need to convert host timestamps.
      return timestamp;
    }

    // Find the offset for |trace_time_clock_id_| and apply the offset, or
    // default offset 0 if not offset is found for |trace_time_clock_id_|.
    int64_t clock_offset = remote_clock_offsets_[trace_time_clock_id_];
    return timestamp - clock_offset;
  }

  ClockId trace_time_clock_id_ = 0;
  std::map<ClockId, ClockDomain> clocks_;
  std::set<ClockGraphEdge> graph_;
  std::set<ClockId> non_monotonic_clocks_;
  std::array<CachedClockPath, 8> cache_{};
  bool cache_lookups_disabled_for_testing_ = false;
  uint32_t cache_hits_for_testing_ = 0;
  std::minstd_rand rnd_;  // For cache eviction.
  uint32_t cur_snapshot_id_ = 0;
  bool trace_time_clock_id_used_for_conversion_ = false;
  base::FlatHashMap<ClockId, int64_t> remote_clock_offsets_;
  std::optional<int64_t> timezone_offset_;
  std::unique_ptr<TClockEventListener> clock_event_listener_;

  // A queue of paths to explore. Stored as a field to reduce allocations
  // on every call to FindPath().
  base::CircularQueue<ClockPath> queue_find_path_cache_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_UTIL_CLOCK_SYNCHRONIZER_H_
