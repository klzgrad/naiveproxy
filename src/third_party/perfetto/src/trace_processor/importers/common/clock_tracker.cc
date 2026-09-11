/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/global_stats_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/clock_synchronizer.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"

namespace perfetto::trace_processor {

// --- ClockTracker: public slow-path methods ---

ClockTracker::ClockTracker(TraceProcessorContext* context,
                           ClockSynchronizer* sync,
                           bool is_primary)
    : context_(context),
      source_clock_id_key_(context->storage->InternString("source_clock_id")),
      target_clock_id_key_(context->storage->InternString("target_clock_id")),
      source_timestamp_key_(context->storage->InternString("source_timestamp")),
      source_sequence_id_key_(
          context->storage->InternString("source_sequence_id")),
      target_sequence_id_key_(
          context->storage->InternString("target_sequence_id")),
      sync_(sync),
      machine_id_(context->machine_id().value),
      own_file_id_(context->trace_id().value),
      is_primary_(is_primary) {
  PERFETTO_CHECK(sync_);
}

base::StatusOr<uint32_t> ClockTracker::AddSnapshot(
    std::vector<ClockTimestamp> clock_timestamps) {
  // A snapshot correlates multiple clock domains, which proves this trace is
  // not single-clock; perfetto_manifest clock overrides are only valid for
  // single-clock traces. This is the chokepoint for all snapshot producers
  // (proto ClockSnapshots, ftrace bundles, instruments).
  if (PERFETTO_UNLIKELY(context_->has_clock_override())) {
    return base::ErrStatus(
        "perfetto_manifest: a `clocks` override without a source `clock` pins "
        "a clockless file to a reference timeline, but this trace emits clock "
        "snapshots. If this is an internally-clocked trace (e.g. Perfetto "
        R"(proto), specify the source clock in the `clocks` block (e.g. )"
        R"("clock": "BOOTTIME"). See )"
        "https://perfetto.dev/docs/reference/perfetto-manifest#clocks");
  }
  // A non-primary trace that brings its own snapshot isolates its builtins onto
  // its own file tag so they cannot corrupt other files' conversions (e.g. an
  // in-app trace + a system trace on the same machine).
  if (PERFETTO_UNLIKELY(!is_primary_ && current_file_tag_ == 0)) {
    if (PERFETTO_UNLIKELY(num_conversions_ > 0)) {
      context_->import_logs_tracker->RecordAnalysisLog(
          stats::clock_sync_mixed_clock_sources,
          [&](ArgsTracker::BoundInserter& inserter) {
            StringId key = context_->storage->InternString(
                "num_conversions_using_primary");
            inserter.AddArg(key, Variadic::UnsignedInteger(num_conversions_));
          });
    }
    current_file_tag_ = own_file_id_;
  }
  // REALTIME is a single universal wall clock and the cross-machine rendezvous
  // domain used to place files that share no other clock (see
  // docs/concepts/merging-traces.md). A non-primary file on a *remote*
  // (non-trace-time) machine reaches trace time only through that rendezvous,
  // so relate each REALTIME clock it actually carries to the machine-canonical
  // REALTIME at zero offset (a twin added to this snapshot). The trace-time
  // machine is excluded: its events reach trace time directly through BOOTTIME.
  const bool bridge_realtime =
      !is_primary_ &&
      machine_id_ != context_->trace_time_state->clock_id.machine_id;
  std::vector<ClockTimestamp> canonical_realtime;
  for (auto& ct : clock_timestamps) {
    const uint32_t clock = ct.clock.id.clock_id;
    ct.clock.id = ClockId::Qualify(ct.clock.id, machine_id_, current_file_tag_);
    if (PERFETTO_UNLIKELY(
            bridge_realtime &&
            (clock == protos::pbzero::BUILTIN_CLOCK_REALTIME ||
             clock == protos::pbzero::BUILTIN_CLOCK_REALTIME_COARSE))) {
      ClockTimestamp twin = ct;
      twin.clock.id = ClockId::Qualify(ClockId::Machine(clock), machine_id_, 0);
      canonical_realtime.push_back(twin);
    }
  }
  clock_timestamps.insert(clock_timestamps.end(), canonical_realtime.begin(),
                          canonical_realtime.end());
  return AddSnapshotInternal(clock_timestamps);
}

base::StatusOr<uint32_t> ClockTracker::AddQualifiedSnapshot(
    const std::vector<ClockTimestamp>& clock_timestamps) {
  return AddSnapshotInternal(clock_timestamps);
}

base::StatusOr<uint32_t> ClockTracker::AddSnapshotInternal(
    const std::vector<ClockTimestamp>& clock_timestamps) {
  ASSIGN_OR_RETURN(uint32_t snapshot_id, sync_->AddSnapshot(clock_timestamps));
  AddSnapshotToTable(context_->storage.get(), sync_,
                     context_->trace_time_state->clock_id, snapshot_id,
                     clock_timestamps);
  return snapshot_id;
}

// static
void ClockTracker::AddSnapshotToTable(
    TraceStorage* storage,
    ClockSynchronizer* sync,
    ClockId trace_time,
    uint32_t snapshot_id,
    const std::vector<ClockTimestamp>& clock_timestamps) {
  // Fallback used when a clock cannot be converted (e.g. non-monotonic): a
  // clock in the snapshot that is itself trace time gives the instant directly.
  std::optional<int64_t> trace_time_from_snapshot;
  for (const auto& ct : clock_timestamps) {
    if (ct.clock.id == trace_time) {
      trace_time_from_snapshot = ct.timestamp;
      break;
    }
  }

  std::optional<int64_t> trace_ts_for_check;
  for (const auto& ct : clock_timestamps) {
    // Incremental clocks map to 0. Convert is error-discarding: failures are
    // expected (non-monotonic clocks) and must not flush deferred syncs or
    // count as event conversions.
    int64_t ts_to_convert = ct.clock.is_incremental ? 0 : ct.timestamp;
    std::optional<int64_t> opt_trace_ts =
        sync->Convert(ct.clock.id, ts_to_convert, trace_time);
    if (!opt_trace_ts) {
      if (!trace_time_from_snapshot) {
        continue;
      }
      opt_trace_ts = trace_time_from_snapshot;
    }
    PERFETTO_DCHECK(!trace_ts_for_check ||
                    *opt_trace_ts == *trace_ts_for_check);
    trace_ts_for_check = opt_trace_ts;

    tables::ClockSnapshotTable::Row row;
    row.ts = *opt_trace_ts;
    row.clock_id = static_cast<int64_t>(ct.clock.id.clock_id);
    row.clock_value = ct.timestamp * ct.clock.unit_multiplier_ns;
    row.clock_name = GetBuiltinClockNameOrNull(storage, ct.clock.id.clock_id);
    row.snapshot_id = snapshot_id;
    row.machine_id = MachineId(ct.clock.id.machine_id);
    storage->mutable_clock_snapshot_table()->Insert(row);
  }
}

// static
std::optional<StringId> ClockTracker::GetBuiltinClockNameOrNull(
    TraceStorage* storage,
    int64_t clock_id) {
  switch (clock_id) {
    case protos::pbzero::BUILTIN_CLOCK_REALTIME:
      return storage->InternString("REALTIME");
    case protos::pbzero::BUILTIN_CLOCK_REALTIME_COARSE:
      return storage->InternString("REALTIME_COARSE");
    case protos::pbzero::BUILTIN_CLOCK_MONOTONIC:
      return storage->InternString("MONOTONIC");
    case protos::pbzero::BUILTIN_CLOCK_MONOTONIC_COARSE:
      return storage->InternString("MONOTONIC_COARSE");
    case protos::pbzero::BUILTIN_CLOCK_MONOTONIC_RAW:
      return storage->InternString("MONOTONIC_RAW");
    case protos::pbzero::BUILTIN_CLOCK_BOOTTIME:
      return storage->InternString("BOOTTIME");
    default:
      return std::nullopt;
  }
}

base::Status ClockTracker::SetGlobalClock(ClockId clock_id) {
  PERFETTO_DCHECK(!clock_id.IsSequenceClock());
  if (num_conversions_ > 0) {
    auto* state = context_->trace_time_state.get();
    return base::ErrStatus(
        "Not updating trace time clock from %s to %s"
        " because the old clock was already used for timestamp "
        "conversion - ClockSnapshot too late in trace?",
        state->clock_id.ToString().c_str(), clock_id.ToString().c_str());
  }
  auto* state = context_->trace_time_state.get();
  // The global clock G is the canonical (machine, file-0) node of the setting
  // machine; cross-machine/cross-file clocks reach it via edges.
  ClockId g = ClockId::Qualify(clock_id, machine_id_, 0);
  if (state->TrySetClock(g, context_->trace_id().value)) {
    context_->metadata_tracker->SetMetadata(
        metadata::trace_time_clock_id, Variadic::Integer(clock_id.clock_id));
  }
  return base::OkStatus();
}

void ClockTracker::SetTraceDefaultClock(ClockId clock_id) {
  trace_default_clock_ = clock_id;
}

void ClockTracker::AddDeferredClockSync(ClockId from,
                                        int64_t from_ts,
                                        std::optional<ClockId> to,
                                        int64_t to_ts) {
  PERFETTO_DCHECK(from_ts >= 0 && to_ts >= 0);
  deferred_clock_sync_ = {from, from_ts, to, to_ts};
}

void ClockTracker::FlushDeferredClockSync() {
  DeferredSync sync = *deferred_clock_sync_;
  deferred_clock_sync_.reset();
  auto* state = context_->trace_time_state.get();
  ClockId g = state->clock_id;

  // The plain identity case (no offset, default target): bridge this file's
  // source clock to trace time via its machine-canonical node, so an isolated
  // file's other clocks stay isolated yet it can still reach trace time.
  if (sync.from_ts == 0 && !sync.to && sync.to_ts == 0) {
    ClockId qualified =
        ClockId::Qualify(sync.from, machine_id_, current_file_tag_);
    ClockId canonical = ClockId::Qualify(sync.from, machine_id_, 0);
    if (qualified != canonical && !sync_->Convert(qualified, 0, canonical)) {
      AddSnapshotInternal({{qualified, 0}, {canonical, 0}});
    }
    BridgeToTraceTime(canonical, g);
    return;
  }

  // The perfetto_manifest clock-pin case: inject the offset edge directly from
  // this file's (qualified) private clock to the requested reference clock (its
  // machine-canonical node, so it joins the shared graph). An omitted target is
  // trace time itself. Real edges (e.g. a proto spine) win, so inject only if
  // |from| cannot already reach |to|.
  ClockId from = ClockId::Qualify(sync.from, machine_id_, current_file_tag_);
  ClockId to = sync.to ? ClockId::Qualify(*sync.to, machine_id_, 0) : g;
  if (from != to && !sync_->Convert(from, sync.from_ts, to)) {
    AddSnapshotInternal({{from, sync.from_ts}, {to, sync.to_ts}});
  }
  // Then bridge the reference clock to trace time the same way every clock
  // reaches it. A machine override can put |to| on a non-host machine with no
  // path to G (its other clocks live in a different machine's domain); this is
  // what lets independent single-machine captures pinned onto a shared clock
  // (e.g. REALTIME) line up on one axis without a remote_clock_sync.
  BridgeToTraceTime(to, g);
}

void ClockTracker::BridgeToTraceTime(ClockId clock_id, ClockId trace_time) {
  // Already related to trace time: a real snapshot, an earlier bridge, or
  // |clock_id| is itself the trace time clock. Nothing to do.
  if (clock_id == trace_time || sync_->Convert(clock_id, 0, trace_time)) {
    return;
  }

  // Prefer aligning on REALTIME. REALTIME is the same absolute (UTC) clock on
  // every machine, so if |clock_id| reaches this machine's REALTIME (there is a
  // path through realtime, i.e. a boot<->realtime snapshot) we tie that
  // REALTIME to the trace time machine's REALTIME (the global rendezvous node)
  // with an assume-aligned zero-offset edge, and |clock_id| reaches trace time
  // through it. Taken only when the rendezvous node itself reaches trace time.
  // A real cross-machine sync (remote_clock_sync), if present, already connects
  // the clocks and wins via the early return above.
  ClockId realtime = ClockId::Qualify(
      ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_REALTIME), machine_id_, 0);
  ClockId global_realtime = ClockId::Machine(
      trace_time.machine_id, protos::pbzero::BUILTIN_CLOCK_REALTIME);
  if (realtime != global_realtime && sync_->Convert(clock_id, 0, realtime) &&
      sync_->Convert(global_realtime, 0, trace_time)) {
    if (!sync_->Convert(realtime, 0, global_realtime)) {
      AddSnapshotInternal({{realtime, 0}, {global_realtime, 0}});
    }
    return;
  }

  // Last resort: nothing relates |clock_id| to the global trace time clock.
  // Inject a zero-offset (assume-aligned) edge only when that assumption is
  // defensible:
  //  - either endpoint is a private per-file clock (BUILTIN_CLOCK_TRACE_FILE):
  //    it has no intrinsic domain and may legitimately map to any clock,
  //    whether it is the source (e.g. a JSON file) or the trace time itself
  //    (e.g. a proto with no ClockSnapshot, whose master timeline is the
  //    synthetic trace-file clock); or
  //  - the two are the same clock domain on different machines/files (the same
  //    physical clock, e.g. two machines' BOOTTIME assumed to share a boot
  //    instant).
  // Across different real domains (e.g. BOOTTIME vs REALTIME) we do not
  // fabricate a relationship: the conversion fails so the events are dropped
  // and logged rather than silently misplaced.
  bool either_private =
      clock_id.clock_id == protos::pbzero::BUILTIN_CLOCK_TRACE_FILE ||
      trace_time.clock_id == protos::pbzero::BUILTIN_CLOCK_TRACE_FILE;
  if (either_private || clock_id.clock_id == trace_time.clock_id) {
    AddSnapshotInternal({{clock_id, 0}, {trace_time, 0}});
    return;
  }

  // Declined: |clock_id| stays unconnected, so its events fail to convert and
  // are dropped (and counted by clock_sync_failure_no_path). Record a clear
  // analysis log explaining why, rather than leaving only that generic
  // no-path error which advises emitting ClockSnapshots.
  context_->import_logs_tracker->RecordAnalysisLog(
      stats::clock_sync_unrelatable_clock_domains,
      [&](ArgsTracker::BoundInserter& inserter) {
        inserter.AddArg(source_clock_id_key_,
                        Variadic::Integer(clock_id.clock_id));
        inserter.AddArg(target_clock_id_key_,
                        Variadic::Integer(trace_time.clock_id));
      });
}

std::optional<int64_t> ClockTracker::timezone_offset() const {
  return context_->trace_time_state->timezone_offset;
}

void ClockTracker::set_timezone_offset(int64_t offset) {
  context_->trace_time_state->timezone_offset = offset;
}

// --- ClockTracker: testing ---

void ClockTracker::set_cache_lookups_disabled_for_testing(bool v) {
  sync_->set_cache_lookups_disabled_for_testing(v);
}

uint32_t ClockTracker::cache_hits_for_testing() const {
  return sync_->cache_hits_for_testing();
}

void ClockTracker::RecordConversionError(const ClockSyncError& error,
                                         std::optional<size_t> byte_offset) {
  size_t stat_key;
  switch (error.type) {
    case ClockSyncErrorType::kUnknownSourceClock:
      stat_key = stats::clock_sync_failure_unknown_source_clock;
      break;
    case ClockSyncErrorType::kUnknownTargetClock:
      stat_key = stats::clock_sync_failure_unknown_target_clock;
      break;
    case ClockSyncErrorType::kNoPath:
      stat_key = stats::clock_sync_failure_no_path;
      break;
  }
  auto args = [&](ArgsTracker::BoundInserter& inserter) {
    if (error.source_clock.seq_id != 0) {
      inserter.AddArg(source_sequence_id_key_,
                      Variadic::UnsignedInteger(error.source_clock.seq_id));
    }
    inserter.AddArg(source_clock_id_key_,
                    Variadic::Integer(error.source_clock.clock_id));
    inserter.AddArg(source_timestamp_key_,
                    Variadic::Integer(error.source_timestamp));
    if (error.target_clock.seq_id != 0) {
      inserter.AddArg(target_sequence_id_key_,
                      Variadic::UnsignedInteger(error.target_clock.seq_id));
    }
    inserter.AddArg(target_clock_id_key_,
                    Variadic::Integer(error.target_clock.clock_id));
  };
  if (byte_offset) {
    context_->import_logs_tracker->RecordTokenizationLog(stat_key, *byte_offset,
                                                         args);
  } else {
    context_->import_logs_tracker->RecordAnalysisLog(stat_key, args);
  }
}

// --- ClockSynchronizerListenerImpl ---

ClockSynchronizerListenerImpl::ClockSynchronizerListenerImpl(
    TraceProcessorContext* context)
    : context_(context) {}

// The cache and clock graph are global (shared by all traces/machines), so
// these diagnostics are recorded globally: a cache miss or an invalid snapshot
// is a property of the shared graph, not of any one trace. Per-conversion
// failures, by contrast, are attributed to the initiating trace by
// ClockTracker::RecordConversionError.
base::Status ClockSynchronizerListenerImpl::OnClockSyncCacheMiss() {
  context_->global_stats_tracker->IncrementStats(std::nullopt, std::nullopt,
                                                 stats::clock_sync_cache_miss);
  return base::OkStatus();
}

base::Status ClockSynchronizerListenerImpl::OnInvalidClockSnapshot() {
  context_->global_stats_tracker->IncrementStats(
      std::nullopt, std::nullopt, stats::invalid_clock_snapshots);
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
