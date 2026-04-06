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
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/circular_queue.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"

namespace perfetto::trace_processor {

// This class handles synchronization of timestamps across different clock
// domains. This includes multi-hop conversions from two clocks A and D, e.g.
// A->B -> B->C -> C->D, even if we never saw a snapshot that contains A and D
// at the same time.
// The API is fairly simple (but the inner operation is not):
// - AddSnapshot(map<clock_id, timestamp>): pushes a set of clocks that have
//   been snapshotted at the same time (within technical limits).
// - Convert(src_clock_id, src_timestamp, target_clock_id):
//   converts a timestamp between two clock domains.
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

// Represents a clock identifier with explicit fields for the raw clock ID,
// the sequence ID (for sequence-scoped clocks), and the trace file ID
// (to isolate sequence-scoped state across different trace files in a TAR).
// Non-sequence clocks have seq_id=0, trace_file_id=0.
struct ClockId {
  uint32_t clock_id = 0;
  uint32_t seq_id = 0;
  uint32_t trace_file_id = 0;

  constexpr ClockId() = default;

  // Factory for machine-scoped builtin clocks (e.g. BOOTTIME, MONOTONIC).
  static constexpr ClockId Machine(uint32_t builtin_clock_id) {
    return {builtin_clock_id, 0, 0};
  }

  // Factory for trace-file-scoped clocks.
  static constexpr ClockId TraceFile(uint32_t tfi) {
    return {protos::pbzero::BUILTIN_CLOCK_TRACE_FILE, 0, tfi};
  }

  // Factory for sequence-scoped clocks.
  static constexpr ClockId Sequence(uint32_t tfi, uint32_t sid, uint32_t cid) {
    PERFETTO_DCHECK(IsSequenceClock(cid));
    return {cid, sid, tfi};
  }

  bool operator==(const ClockId& o) const {
    return clock_id == o.clock_id && seq_id == o.seq_id &&
           trace_file_id == o.trace_file_id;
  }
  bool operator!=(const ClockId& o) const { return !(*this == o); }
  bool operator<(const ClockId& o) const {
    return std::tie(clock_id, seq_id, trace_file_id) <
           std::tie(o.clock_id, o.seq_id, o.trace_file_id);
  }

  std::string ToString() const;

  template <typename H>
  friend H PerfettoHashValue(H h, const ClockId& c) {
    return H::Combine(std::move(h), c.clock_id, c.seq_id, c.trace_file_id);
  }

  static constexpr bool IsSequenceClock(uint32_t clock_id) {
    return clock_id >= 64 && clock_id < 128;
  }

  bool IsSequenceClock() const { return IsSequenceClock(clock_id); }

 private:
  friend class ClockSynchronizer;

  constexpr ClockId(uint32_t cid, uint32_t sid, uint32_t tfi)
      : clock_id(cid), seq_id(sid), trace_file_id(tfi) {}
};

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

// Error type when clock conversion fails.
enum class ClockSyncErrorType {
  kOk = 0,
  kUnknownSourceClock,  // Source clock never seen in any snapshot
  kUnknownTargetClock,  // Target clock never seen in any snapshot
  kNoPath,              // No snapshot path connects source to target
};

// Shared state for the trace time clock. Owned externally (e.g. by
// TraceProcessorContext) and passed by pointer to ClockSynchronizer so
// that AddSnapshot can validate against the current trace-time clock
// without a virtual call.
struct TraceTimeState {
  TraceTimeState() = default;
  explicit TraceTimeState(ClockId _clock_id) : clock_id(_clock_id) {}

  ClockId clock_id;

  // Which trace file owns the trace time clock. Only the owner (or no owner)
  // may change clock_id. Prevents secondary traces in a ZIP from overriding
  // the primary trace's clock choice.
  std::optional<uint32_t> trace_time_clock_owner;

  std::optional<int64_t> timezone_offset;

  // TODO(lalitm): remote_clock_offsets is a hack. We should have a proper
  // definition for dealing with cross-machine clock synchronization.
  base::FlatHashMap<ClockId, int64_t> remote_clock_offsets;
};

// Virtual interface for listening to clock synchronization events.
// All methods are on slow paths, so virtual dispatch overhead is negligible.
class ClockSynchronizerListener {
 public:
  virtual ~ClockSynchronizerListener();
  virtual base::Status OnClockSyncCacheMiss() = 0;
  virtual base::Status OnInvalidClockSnapshot() = 0;
  virtual void RecordConversionError(ClockSyncErrorType,
                                     ClockId,
                                     ClockId,
                                     int64_t,
                                     std::optional<size_t>) = 0;
};

class ClockSynchronizer {
 public:
  ClockSynchronizer(TraceTimeState* trace_time_state,
                    std::unique_ptr<ClockSynchronizerListener> listener);

  // Returns true if the clock synchronizer has seen the given clock in any
  // snapshot.
  bool HasClock(ClockId clock_id) const {
    return clocks_.find(clock_id) != clocks_.end();
  }

  // Appends a new snapshot for the given clock domains.
  // This is typically called by the code that reads the ClockSnapshot packet.
  // Returns the internal snapshot id of this set of clocks.
  base::StatusOr<uint32_t> AddSnapshot(
      const std::vector<ClockTimestamp>& clock_timestamps);

  // Converts a timestamp between two clock domains. Tries to use the cache
  // first (only for single-path resolutions), then falls back on path finding
  // as described in the header.
  std::optional<int64_t> Convert(ClockId src_clock_id,
                                 int64_t src_timestamp,
                                 ClockId target_clock_id,
                                 std::optional<size_t> byte_offset) {
    if (PERFETTO_LIKELY(src_clock_id == target_clock_id)) {
      return src_timestamp;
    }
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
    return ConvertSlowpath(src_clock_id, src_timestamp, ns, target_clock_id,
                           byte_offset);
  }

  // For testing:
  void set_cache_lookups_disabled_for_testing(bool v) {
    cache_lookups_disabled_for_testing_ = v;
  }

  uint32_t cache_hits_for_testing() const { return cache_hits_for_testing_; }

 private:
  using SnapshotHash = uint32_t;

  // 0th argument is the source clock, 1st argument is the target clock.
  using ClockGraphEdge = std::tuple<ClockId, ClockId, SnapshotHash>;

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
    ClockId last;
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

  std::optional<int64_t> ConvertSlowpath(ClockId src_clock_id,
                                         int64_t src_timestamp,
                                         std::optional<int64_t> src_ts_ns,
                                         ClockId target_clock_id,
                                         std::optional<size_t> byte_offset);

  // Returns whether |global_clock_id| represents a sequence-scoped clock, i.e.
  // a ClockId returned by SequenceToGlobalClock().
  static bool IsConvertedSequenceClock(ClockId global_clock_id) {
    return global_clock_id.seq_id != 0;
  }

  // Finds the shortest clock resolution path in the graph that allows to
  // translate a timestamp from |src| to |target| clocks.
  ClockPath FindPath(ClockId src, ClockId target);

  ClockDomain* GetClock(ClockId clock_id);

  TraceTimeState* trace_time_state_;
  std::map<ClockId, ClockDomain> clocks_;
  std::set<ClockGraphEdge> graph_;
  std::set<ClockId> non_monotonic_clocks_;
  std::array<CachedClockPath, 8> cache_{};
  bool cache_lookups_disabled_for_testing_ = false;
  uint32_t cache_hits_for_testing_ = 0;
  std::minstd_rand rnd_;  // For cache eviction.
  uint32_t cur_snapshot_id_ = 0;
  std::unique_ptr<ClockSynchronizerListener> clock_event_listener_;

  // A queue of paths to explore. Stored as a field to reduce allocations
  // on every call to FindPath().
  base::CircularQueue<ClockPath> queue_find_path_cache_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_UTIL_CLOCK_SYNCHRONIZER_H_
