/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/importers/common/trace_diagnostics_tracker_helper.h"

#include <cstdint>
#include <optional>

#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/clock_synchronizer.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"

namespace perfetto::trace_processor {
namespace {

// Returns true if any stat row scoped to `context`'s (trace, machine) has a
// value > 0 and satisfies `pred(key)`.
template <typename Pred>
bool AnyPositiveStat(TraceProcessorContext* context, Pred pred) {
  const auto machine_id = context->machine_id();
  const auto trace_id = context->trace_id();
  for (auto it = context->storage->stats_table().IterateRows(); it; ++it) {
    if (it.value() <= 0)
      continue;
    // These stats are (mostly) kMachineAndTrace-scoped; only count this
    // context's rows.
    if (it.machine_id() != machine_id || it.trace_id() != trace_id)
      continue;
    if (pred(static_cast<size_t>(it.key())))
      return true;
  }
  return false;
}

}  // namespace

void TraceDiagnosticsHelper::AddTraceDiagnostic(base::StringView key,
                                                base::StringView title,
                                                base::StringView description,
                                                base::StringView remediation,
                                                double confidence) {
  tables::TraceDiagnosticsTable::Row row;
  row.key = context_->storage->InternString(key);
  row.title = context_->storage->InternString(title);
  row.description = context_->storage->InternString(description);
  row.remediation = context_->storage->InternString(remediation);
  row.confidence = confidence;
  row.trace_id = context_->trace_id();
  context_->storage->mutable_trace_diagnostics_table()->Insert(row);
}

std::optional<int64_t> TraceDiagnosticsHelper::TracingStartedSinceBootNs()
    const {
  // The trace records when tracing started (a TracingServiceEvent) as the
  // `tracing_started_ns` metadata, in the trace-time domain.
  std::optional<SqlValue> started =
      context_->metadata_tracker->GetMetadata(metadata::tracing_started_ns);
  if (!started.has_value() || started->type != SqlValue::kLong)
    return std::nullopt;

  // Convert into the BOOTTIME domain: in that domain the value is nanoseconds
  // since boot.
  using ClockId = ClockTracker::ClockId;
  std::optional<int64_t> boot_ns = context_->clock_tracker->Convert(
      context_->trace_time_state->clock_id, started->AsLong(),
      ClockId::Machine(protos::pbzero::BUILTIN_CLOCK_BOOTTIME));

  // If the clock graph can't bridge trace-time to BOOTTIME (e.g. no clock
  // snapshot was recorded), assume the trace clock is already CLOCK_BOOTTIME
  // and use the raw value. This is the common case for Android/Linux traces.
  return boot_ns.value_or(started->AsLong());
}

bool TraceDiagnosticsHelper::HasFtraceCpuDataLoss() const {
  return AnyPositiveStat(context_, [](size_t key) {
    return key == stats::ftrace_cpu_has_data_loss ||
           key == stats::ftrace_cpu_overrun_delta ||
           key == stats::ftrace_cpu_commit_overrun_delta;
  });
}

bool TraceDiagnosticsHelper::HasTracedDataLoss() const {
  return AnyPositiveStat(context_, [](size_t key) {
    return stats::kSeverities[key] == stats::kDataLoss &&
           base::StringView(stats::kNames[key]).StartsWith("traced_");
  });
}

bool TraceDiagnosticsHelper::HasHeapprofdErrorStats() const {
  return AnyPositiveStat(context_, [](size_t key) {
    return stats::kSeverities[key] == stats::kError &&
           base::StringView(stats::kNames[key]).StartsWith("heapprofd");
  });
}

bool TraceDiagnosticsHelper::IsAndroidUserBuild() const {
  std::optional<SqlValue> fp = context_->metadata_tracker->GetMetadata(
      metadata::android_build_fingerprint);
  if (!fp.has_value() || fp->type != SqlValue::kString)
    return false;
  // The fingerprint is brand/product/device:release/id/incr:type/tags, so the
  // build type is the token after the last ':' up to the following '/'.
  base::StringView s(fp->AsString());
  size_t colon = s.rfind(':');
  if (colon == base::StringView::npos)
    return false;
  base::StringView rest = s.substr(colon + 1);
  size_t slash = rest.find('/');
  base::StringView type =
      slash == base::StringView::npos ? rest : rest.substr(0, slash);
  return type == base::StringView("user");
}

bool TraceDiagnosticsHelper::HasVideoFrames() const {
  return context_->storage->has_android_video_frames();
}

bool TraceDiagnosticsHelper::HasVideoErrorStats() const {
  return AnyPositiveStat(context_, [](size_t key) {
    return (stats::kSeverities[key] == stats::kError ||
            stats::kSeverities[key] == stats::kDataLoss) &&
           base::StringView(stats::kNames[key]).StartsWith("android_video_");
  });
}

}  // namespace perfetto::trace_processor
