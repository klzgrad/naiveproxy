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

#include "src/trace_processor/forwarding_trace_parser.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/global_stats_tracker.h"
#include "src/trace_processor/importers/common/machine_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/trace_reader_registry.h"
#include "src/trace_processor/types/trace_manifest_state.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/clock_synchronizer.h"
#include "src/trace_processor/util/trace_type.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"

namespace perfetto::trace_processor {
namespace {

TraceSorter::SortingMode ConvertSortingMode(SortingMode sorting_mode) {
  switch (sorting_mode) {
    case SortingMode::kDefaultHeuristics:
      return TraceSorter::SortingMode::kDefault;
    case SortingMode::kForceFullSort:
      return TraceSorter::SortingMode::kFullSort;
  }
  PERFETTO_FATAL("For GCC");
}

std::optional<TraceSorter::SortingMode> GetMinimumSortingMode(
    TraceImporterId trace_type,
    const TraceProcessorContext& context) {
  const TraceTypeDescriptor* d =
      context.trace_importer_registry->Find(trace_type);
  PERFETTO_CHECK(d);
  switch (d->sort_policy) {
    case TraceSortPolicy::kFullSort:
      return TraceSorter::SortingMode::kFullSort;
    case TraceSortPolicy::kConfigDriven:
      return ConvertSortingMode(context.config.sorting_mode);
    case TraceSortPolicy::kNone:
      return std::nullopt;
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace

ForwardingTraceParser::ForwardingTraceParser(TraceProcessorContext* context,
                                             tables::TraceFileTable::Id id)
    : input_context_(context), file_id_(id) {}

ForwardingTraceParser::~ForwardingTraceParser() = default;

base::Status ForwardingTraceParser::Init(const TraceBlobView& blob) {
  PERFETTO_CHECK(!reader_);

  {
    auto scoped_trace =
        input_context_->global_stats_tracker->TraceExecutionTimeIntoStats(
            stats::guess_trace_type_duration_ns);
    trace_type_ = input_context_->trace_importer_registry->Guess(blob.data(),
                                                                 blob.size());
  }
  if (!trace_type_) {
    // If renaming this error message don't remove the "(ERR:fmt)" part.
    // The UI's error_dialog.ts uses it to make the dialog more graceful.
    return base::ErrStatus("Unknown trace type provided (ERR:fmt)");
  }
  PERFETTO_DLOG("%s trace detected",
                input_context_->trace_importer_registry->ToString(trace_type_));

  const TraceTypeDescriptor* desc =
      input_context_->trace_importer_registry->Find(trace_type_);
  PERFETTO_CHECK(desc);

  if (file_id_.value != 0 && !desc->supports_nesting) {
    return base::ErrStatus(
        "Ninja traces currently do not support being contained inside other "
        "trace formats. Please file a bug at "
        "https://github.com/google/perfetto/issues if this is important to "
        "you.");
  }

  // A perfetto_manifest file configures the parsing of the files which
  // follow it, so it is only valid before any non-container trace. Archive
  // sorting guarantees this for direct members; this rejects e.g. a
  // gzip-wrapped metadata file sorted after a proto trace.
  if (desc->is_manifest &&
      input_context_->forked_context_state->trace_to_context.size() != 0) {
    return base::ErrStatus(
        "perfetto_manifest file must be the first trace file in the input");
  }

  std::optional<TraceSorter::SortingMode> minimum_sorting_mode =
      GetMinimumSortingMode(trace_type_, *input_context_);
  if (minimum_sorting_mode) {
    input_context_->sorter->SetSortingMode(*minimum_sorting_mode);
  }
  input_context_->trace_file_tracker->StartParsing(file_id_, trace_type_);

  // If the perfetto_manifest file has an entry for this file (matched by
  // exact path), it overrides clock/machine handling below.
  TraceManifestState::FileEntry* manifest_entry = FindManifestEntry();
  if (manifest_entry &&
      (manifest_entry->clock_override || manifest_entry->machine_id) &&
      !desc->forks_context) {
    return base::ErrStatus(
        "perfetto_manifest: overrides are not supported for trace files "
        "which are themselves archives or perfetto_manifest files: %s",
        manifest_entry->path.c_str());
  }

  if (!desc->forks_context) {
    // perfetto_manifest files produce no events: like containers they must
    // not fork a per-trace context, as that would make this file the
    // "primary" trace for its machine and demote the real traces.
    PERFETTO_DCHECK(!input_context_->trace_state);
    trace_context_ = input_context_;
  } else {
    int64_t raw_machine_id = manifest_entry && manifest_entry->machine_id
                                 ? *manifest_entry->machine_id
                                 : 0;
    // TODO(b/334978369) Make sure proto and systrace traces are parsed first so
    // that we do not get issues with SetPidZeroIsUpidZeroIdleProcess()
    // The machine row was pre-allocated by the manifest reader (which also
    // named it); this fork reuses it via MachineTracker.
    trace_context_ =
        input_context_->ForkContextForTrace(file_id_, raw_machine_id);
    if (desc->pid_zero_is_idle) {
      trace_context_->process_tracker->SetPidZeroIsUpidZeroIdleProcess();
    }
    if (manifest_entry) {
      // A `machines` block declares the file IS multi-machine, so it is not a
      // single-machine override; instead the proto dispatcher remaps embedded
      // ids through it.
      bool is_multi = !manifest_entry->machine_mappings.empty();
      trace_context_->trace_state->has_machine_override =
          manifest_entry->machine_id.has_value() && !is_multi;
      if (is_multi) {
        trace_context_->trace_state->machine_remap =
            &manifest_entry->machine_remap;
      }
    }
  }
  ASSIGN_OR_RETURN(reader_, input_context_->reader_registry->CreateTraceReader(
                                trace_type_, trace_context_, file_id_.value));

  // Centralize clock setup for all trace formats. Every format declares the
  // clock domain its native timestamps are expressed in (its "trace clock"),
  // and we do three things with it:
  //
  //   1. Record it as the file's default clock, which tokenizers convert their
  //      events through via ClockTracker::ConvertDefaultClockToTraceTime. Proto
  //      is the exception (see below).
  //
  //   2. Claim it as the global trace-time clock. The first trace to claim
  //      wins; later claims are silently ignored, so the global clock is
  //      stable regardless of how many traces an archive (e.g. a ZIP) holds.
  //
  //   3. Register a deferred clock sync for the same clock (an implicit edge).
  //      If this trace does NOT win the global clock (e.g. a proto trace in the
  //      same archive claimed BOOTTIME first) and the trace clock is not
  //      otherwise linked into the clock graph via a ClockSnapshot, a
  //      zero-offset identity edge is injected on the first conversion. This
  //      keeps the trace's timestamps convertible instead of silently dropping
  //      every event. When a real ClockSnapshot does link the clock (e.g. proto
  //      provides BOOTTIME<->MONOTONIC), that real relationship is used
  //      instead.
  //
  // Proto traces are special: their clock is whatever ParseClockSnapshot reads
  // from primary_trace_clock, set later, so here we only register BOOTTIME as
  // the deferred fallback, do not set the default clock, and do not claim the
  // global clock now.
  //
  // A perfetto_manifest clock override for this file replaces the format's
  // best-effort source clock (below) with the file's own private clock and
  // customizes its implicit edge into the graph (see the manifest branch).
  using ClockId = ClockTracker::ClockId;
  std::optional<ClockId> trace_clock;
  switch (desc->clock_policy) {
    case TraceClockPolicy::kNone:
      break;
    case TraceClockPolicy::kMonotonic:
      trace_clock = ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_MONOTONIC);
      break;
    case TraceClockPolicy::kBoottime:
      trace_clock = ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
      break;
    case TraceClockPolicy::kRealtime:
      trace_clock = ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_REALTIME);
      break;
    case TraceClockPolicy::kTraceFile:
      trace_clock = ClockId::TraceFile(trace_context_->trace_id().value);
      break;
  }
  auto& clock_tracker = trace_context_->clock_tracker;

  // A perfetto_manifest "manual" clock override relates this file's clock to a
  // clock in another trace; the manifest reader has already added every such
  // edge to the global graph. A pinned (clockless) source also has no real
  // clock of its own, so flag it: the file is now single-clock / single-machine
  // and any ClockSnapshot or remote machine id on it is rejected. It still
  // converts through the default clock set up below, like any clockless format.
  if (manifest_entry && manifest_entry->clock_override &&
      !manifest_entry->clock_override->source_clock) {
    trace_context_->trace_state->has_clock_override = true;
  }

  // Set up the format's source clock. Proto manages its own default clock
  // (primary_trace_clock / ClockSnapshot) so it does not set the default clock
  // (sets_default_clock=false); every other format converts its events through
  // the default clock via ClockTracker::ConvertDefaultClockToTraceTime.
  if (trace_clock) {
    if (desc->sets_default_clock) {
      clock_tracker->SetTraceDefaultClock(*trace_clock);
    }
    if (desc->claims_global_clock) {
      clock_tracker->SetGlobalClock(*trace_clock);
    }
    clock_tracker->AddDeferredClockSync(*trace_clock);
  }
  return base::OkStatus();
}

TraceManifestState::FileEntry* ForwardingTraceParser::FindManifestEntry()
    const {
  auto* state = input_context_->trace_manifest_state.get();
  if (state->files.empty()) {
    return nullptr;
  }
  auto row = input_context_->storage->trace_file_table()[file_id_];
  if (!row.name()) {
    return nullptr;
  }
  return state->FindEntry(
      input_context_->storage->GetString(*row.name()).ToStdString());
}

base::Status ForwardingTraceParser::Parse(TraceBlobView blob) {
  // If this is the first Parse() call, guess the trace type and create the
  // appropriate parser.
  if (!reader_) {
    RETURN_IF_ERROR(Init(blob));
  }
  trace_size_ += blob.size();
  return reader_->Parse(std::move(blob));
}

base::Status ForwardingTraceParser::OnPushDataToSorter() {
  if (reader_) {
    return reader_->OnPushDataToSorter();
  }
  return base::OkStatus();
}

void ForwardingTraceParser::OnEventsFullyExtracted() {
  if (reader_) {
    reader_->OnEventsFullyExtracted();
  }
  if (trace_type_) {
    input_context_->trace_file_tracker->DoneParsing(file_id_, trace_size_);
  }
}

}  // namespace perfetto::trace_processor
