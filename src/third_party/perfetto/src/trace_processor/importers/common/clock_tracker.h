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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_CLOCK_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_CLOCK_TRACKER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/clock_synchronizer.h"

namespace perfetto::trace_processor {

class ClockTrackerTest;

class ClockSynchronizerListenerImpl;

// ClockTracker wraps ClockSynchronizer (the pure conversion engine) and adds
// trace-time semantics: which clock domain is the "trace time", applying
// remote-machine offsets, and writing metadata.
class ClockTracker {
 public:
  // Re-export types for callers that use ClockTracker::ClockId etc.
  using ClockId = ::perfetto::trace_processor::ClockId;
  using ClockTimestamp = ::perfetto::trace_processor::ClockTimestamp;
  using Clock = ::perfetto::trace_processor::Clock;

  // |sync| is the single global clock graph shared by all traces/machines.
  // This tracker stamps every clock it touches with its (machine, file) tag so
  // its clocks are isolated from other files'. |is_primary| (first trace of its
  // machine) keeps its builtins on the machine-canonical tag (f=0) so later
  // snapshot-less files can resolve through them; non-primary files that bring
  // their own snapshots isolate onto their own file tag.
  ClockTracker(TraceProcessorContext* context,
               ClockSynchronizer* sync,
               bool is_primary);

  // --- Hot-path APIs (inlined) ---

  // Converts a timestamp to the trace time domain. On the first call, also
  // finalizes the trace time clock, preventing it from being changed later.
  PERFETTO_ALWAYS_INLINE std::optional<int64_t> ToTraceTime(
      ClockId clock_id,
      int64_t timestamp,
      std::optional<size_t> byte_offset = std::nullopt,
      bool suppress_errors = false) {
    if (PERFETTO_UNLIKELY(deferred_clock_sync_.has_value())) {
      FlushDeferredClockSync();
    }
    auto* state = context_->trace_time_state.get();
    ++num_conversions_;
    std::optional<int64_t> ts = sync_->Convert(
        ClockId::Qualify(clock_id, machine_id_, current_file_tag_), timestamp,
        state->clock_id);
    if (PERFETTO_UNLIKELY(!ts && !suppress_errors))
      RecordConversionError(sync_->last_error(), byte_offset);
    return ts;
  }

  // Converts |ts|, expressed in this trace file's default clock (set via
  // SetTraceDefaultClock), to trace time. Single-clock tokenizers call this
  // instead of naming a builtin, so a perfetto_manifest override that swaps the
  // default clock for the file's private clock transparently redirects them.
  PERFETTO_ALWAYS_INLINE std::optional<int64_t> ConvertDefaultClockToTraceTime(
      int64_t ts,
      std::optional<size_t> byte_offset = std::nullopt,
      bool suppress_errors = false) {
    PERFETTO_DCHECK(trace_default_clock_.has_value());
    return ToTraceTime(*trace_default_clock_, ts, byte_offset, suppress_errors);
  }

  // Converts a timestamp between two arbitrary clock domains.
  PERFETTO_ALWAYS_INLINE std::optional<int64_t> Convert(
      ClockId src,
      int64_t ts,
      ClockId target,
      std::optional<size_t> byte_offset = {}) {
    ++num_conversions_;
    std::optional<int64_t> converted = sync_->Convert(
        ClockId::Qualify(src, machine_id_, current_file_tag_), ts,
        ClockId::Qualify(target, machine_id_, current_file_tag_));
    if (PERFETTO_UNLIKELY(!converted))
      RecordConversionError(sync_->last_error(), byte_offset);
    return converted;
  }

  // --- Slow-path public APIs ---

  // Adds a clock snapshot to the clock graph and records it in the
  // clock_snapshot table (one row per clock, converted to trace time, best
  // effort), which backs ClockConverter (to_realtime, abs_time_str, the UI
  // wall-clock axis). Every edge entering the graph is recorded this way;
  // deferred syncs record when their edge is actually injected.
  // Taken by value and (machine, file)-qualified in place; callers pass
  // temporaries, so it moves in without a copy.
  base::StatusOr<uint32_t> AddSnapshot(
      std::vector<ClockTimestamp> clock_timestamps);

  // Adds a snapshot whose clocks are already fully (machine, file) qualified,
  // bypassing this tracker's own tagging. Used for cross-machine edges (e.g.
  // remote_clock_sync), which relate clocks on two different machines.
  base::StatusOr<uint32_t> AddQualifiedSnapshot(
      const std::vector<ClockTimestamp>& clock_timestamps);

  // Records a snapshot already added to the graph (which assigned it
  // |snapshot_id|) into the clock_snapshot table: one best-effort row per
  // clock, tagged with the clock's own machine. Static so the perfetto_manifest
  // reader, which has no per-machine tracker, can record the cross-machine
  // edges it adds straight to the global graph.
  static void AddSnapshotToTable(
      TraceStorage* storage,
      ClockSynchronizer* sync,
      ClockId trace_time,
      uint32_t snapshot_id,
      const std::vector<ClockTimestamp>& clock_timestamps);

  // --- Low-level clock primitives. Do not call without understanding the
  // --- consequences. Most callers should use the helpers below these.

  // Sets the global trace time clock (trace_time_state->clock_id).
  // All TP timestamps will be converted to this clock domain.
  // Returns error if called after conversions have already happened.
  base::Status SetGlobalClock(ClockId clock_id);

  // Sets the default clock for this trace file only (no global effect).
  // Used as fallback when no timestamp_clock_id is specified.
  void SetTraceDefaultClock(ClockId clock_id);

  // Registers a deferred clock edge, flushed on the first ToTraceTime call.
  // With all defaults this is the plain identity-to-trace-time edge every trace
  // file registers for its source clock: if |from| cannot already reach trace
  // time through the graph, a zero-offset edge is injected (bridged via this
  // file's machine-canonical node so isolated files still resolve).
  // A perfetto_manifest clock pin passes a non-zero |from_ts|/|to_ts| offset
  // and/or an explicit |to| target (omitted means trace time); that edge is
  // injected directly from this file's (qualified) |from| clock to |to|.
  // All timestamps must be non-negative.
  void AddDeferredClockSync(ClockId from,
                            int64_t from_ts = 0,
                            std::optional<ClockId> to = std::nullopt,
                            int64_t to_ts = 0);

  // Returns the trace default clock, if one has been set.
  std::optional<ClockId> trace_default_clock() const {
    return trace_default_clock_;
  }

  std::optional<int64_t> timezone_offset() const;
  void set_timezone_offset(int64_t offset);

  // --- Testing ---
  void set_cache_lookups_disabled_for_testing(bool v);
  uint32_t cache_hits_for_testing() const;

 private:
  friend class ClockTrackerTest;

  // Records a failed clock conversion in this trace's import log, with the
  // source/target clock ids and source timestamp as args (plus |byte_offset|
  // for a tokenization error, else an analysis error).
  PERFETTO_NO_INLINE void RecordConversionError(
      const ClockSyncError& error,
      std::optional<size_t> byte_offset);

  PERFETTO_NO_INLINE void FlushDeferredClockSync();

  // Guarantees a flushed |clock_id| can reach the trace time clock. If the
  // graph already relates them, does nothing. Otherwise prefers a cross-machine
  // REALTIME alignment (routing through the trace time machine's REALTIME as a
  // global rendezvous node), falling back to assuming |clock_id| is itself
  // aligned with trace time. Both fallbacks are zero-offset (assume-aligned)
  // edges; a real relationship always wins.
  void BridgeToTraceTime(ClockId clock_id, ClockId trace_time);

  // Adds an edge directly to the global sync and records it in the
  // clock_snapshot table. Every graph mutation goes through here so the table
  // is a faithful record of the graph. Clocks must already be qualified.
  base::StatusOr<uint32_t> AddSnapshotInternal(
      const std::vector<ClockTimestamp>& clock_timestamps);

  // Returns the interned name for a builtin clock, or nullopt if |clock_id| is
  // not a builtin clock.
  static std::optional<StringId> GetBuiltinClockNameOrNull(
      TraceStorage* storage,
      int64_t clock_id);

  TraceProcessorContext* context_;

  // Interned arg keys for conversion-error import logs, populated in the
  // constructor.
  StringId source_clock_id_key_;
  StringId target_clock_id_key_;
  StringId source_timestamp_key_;
  StringId source_sequence_id_key_;
  StringId target_sequence_id_key_;

  // The single global clock graph, shared by all trace/machine trackers.
  ClockSynchronizer* sync_;

  // This tracker's machine and trace-file ids, used to (machine, file) qualify
  // the clocks it touches.
  uint32_t machine_id_ = 0;
  uint32_t own_file_id_ = 0;

  // The file tag stamped onto this tracker's builtins: 0 (machine-canonical)
  // for primary traces and for non-primary traces until they bring their own
  // snapshot, then |own_file_id_| (isolated). See AddSnapshot.
  uint32_t current_file_tag_ = 0;

  // Whether this is the primary (first) trace of its machine. Non-primary
  // traces isolate onto their own file tag once they add a snapshot.
  bool is_primary_ = true;

  // Total number of conversions performed.
  uint32_t num_conversions_ = 0;

  // The default clock for this trace file, set via SetTraceDefaultClock.
  // Used by proto_trace_reader as a fallback when no timestamp_clock_id
  // is specified.
  std::optional<ClockId> trace_default_clock_;

  // Edge registered via AddDeferredClockSync, flushed (and cleared) on the
  // first ToTraceTime call. |to| == nullopt means the trace time clock,
  // resolved at flush time.
  struct DeferredSync {
    ClockId from;
    int64_t from_ts;
    std::optional<ClockId> to;
    int64_t to_ts;
  };
  std::optional<DeferredSync> deferred_clock_sync_;
};

class ClockSynchronizerListenerImpl : public ClockSynchronizerListener {
 public:
  explicit ClockSynchronizerListenerImpl(TraceProcessorContext* context);

  base::Status OnClockSyncCacheMiss() override;

  base::Status OnInvalidClockSnapshot() override;

 private:
  TraceProcessorContext* context_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_CLOCK_TRACKER_H_
