/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/ftrace/pixel_mm_kswapd_event_tracker.h"

#include <cmath>
#include <cstdint>

#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/pixel_mm.pbzero.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

PixelMmKswapdEventTracker::PixelMmKswapdEventTracker(
    TraceProcessorContext* context)
    : context_(context),
      kswapd_efficiency_name_(
          context->storage->InternString("kswapd_efficiency")),
      efficiency_pct_name_(context->storage->InternString("efficiency %")),
      pages_scanned_name_(context->storage->InternString("pages scanned")),
      pages_reclaimed_name_(context->storage->InternString("pages reclaimed")),
      pages_allocated_name_(context->storage->InternString("pages allocated")) {
}

void PixelMmKswapdEventTracker::ParsePixelMmKswapdWake(int64_t timestamp,
                                                       uint32_t pid) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId details_track = context_->track_tracker->InternThreadTrack(utid);

  context_->slice_tracker->Begin(timestamp, details_track, kNullStringId,
                                 kswapd_efficiency_name_);
}

void PixelMmKswapdEventTracker::ParsePixelMmKswapdDone(
    int64_t timestamp,
    uint32_t pid,
    protozero::ConstBytes blob) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId details_track = context_->track_tracker->InternThreadTrack(utid);

  protos::pbzero::PixelMmKswapdDoneFtraceEvent::Decoder decoder(blob);

  context_->slice_tracker->End(
      timestamp, details_track, kNullStringId, kswapd_efficiency_name_,
      [this, &decoder](ArgsTracker::BoundInserter* inserter) {
        if (decoder.has_delta_nr_scanned()) {
          inserter->AddArg(
              pages_scanned_name_,
              Variadic::UnsignedInteger(decoder.delta_nr_scanned()));
        }
        if (decoder.has_delta_nr_reclaimed()) {
          inserter->AddArg(
              pages_reclaimed_name_,
              Variadic::UnsignedInteger(decoder.delta_nr_reclaimed()));
        }

        if (decoder.has_delta_nr_reclaimed() &&
            decoder.has_delta_nr_scanned()) {
          double efficiency =
              static_cast<double>(decoder.delta_nr_reclaimed()) * 100 /
              static_cast<double>(decoder.delta_nr_scanned());

          inserter->AddArg(efficiency_pct_name_,
                           Variadic::UnsignedInteger(
                               static_cast<uint64_t>(std::round(efficiency))));
        }

        if (decoder.has_delta_nr_allocated()) {
          inserter->AddArg(
              pages_allocated_name_,
              Variadic::UnsignedInteger(decoder.delta_nr_allocated()));
        }
      });
}

}  // namespace perfetto::trace_processor
