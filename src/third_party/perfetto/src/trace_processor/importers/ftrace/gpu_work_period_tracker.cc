/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/importers/ftrace/gpu_work_period_tracker.h"

#include <cstdint>

#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/power.pbzero.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

GpuWorkPeriodTracker::GpuWorkPeriodTracker(TraceProcessorContext* context)
    : context_(context),
      start_time_ns_key_id_(context_->storage->InternString("start_time_ns")),
      end_time_ns_key_id_(context_->storage->InternString("end_time_ns")),
      gpu_id_key_id_(context_->storage->InternString("gpu_id")) {}

void GpuWorkPeriodTracker::ParseGpuWorkPeriodEvent(int64_t timestamp,
                                                   protozero::ConstBytes blob) {
  protos::pbzero::GpuWorkPeriodFtraceEvent::Decoder evt(blob);

  static constexpr auto kTrackBlueprint = tracks::SliceBlueprint(
      "android_gpu_work_period",
      tracks::DimensionBlueprints(tracks::kGpuDimensionBlueprint,
                                  tracks::kUidDimensionBlueprint));
  TrackId track_id = context_->track_tracker->InternTrack(
      kTrackBlueprint, {evt.gpu_id(), static_cast<int32_t>(evt.uid())});

  const auto duration =
      static_cast<int64_t>(evt.end_time_ns() - evt.start_time_ns());
  if (duration < 0) {
    context_->import_logs_tracker->RecordParserError(
        stats::gpu_work_period_negative_duration, timestamp,
        [&](ArgsTracker::BoundInserter& inserter) {
          inserter.AddArg(
              start_time_ns_key_id_,
              Variadic::Integer(static_cast<int64_t>(evt.start_time_ns())));
          inserter.AddArg(
              end_time_ns_key_id_,
              Variadic::Integer(static_cast<int64_t>(evt.end_time_ns())));
          inserter.AddArg(
              gpu_id_key_id_,
              Variadic::Integer(static_cast<int64_t>(evt.gpu_id())));
        });
    return;
  }
  const auto active_duration =
      static_cast<int64_t>(evt.total_active_duration_ns());
  const double active_percent = 100.0 * (static_cast<double>(active_duration) /
                                         static_cast<double>(duration));

  base::StackString<255> entry_name("%%%.2f", active_percent);
  StringId entry_name_id =
      context_->storage->InternString(entry_name.string_view());

  auto slice_id = context_->slice_tracker->Scoped(
      timestamp, track_id, kNullStringId, entry_name_id, duration);
  if (slice_id) {
    auto rr = context_->storage->mutable_slice_table()->FindById(*slice_id);
    rr->set_thread_ts(timestamp);
    rr->set_thread_dur(active_duration);
  }
}

}  // namespace perfetto::trace_processor
