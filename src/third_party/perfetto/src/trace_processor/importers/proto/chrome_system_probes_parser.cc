/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/chrome_system_probes_parser.h"

#include <cstdint>

#include "perfetto/protozero/proto_decoder.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/perfetto/trace/ps/process_stats.pbzero.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

ChromeSystemProbesParser::ChromeSystemProbesParser(
    TraceProcessorContext* context)
    : context_(context),
      is_peak_rss_resettable_id_(
          context->storage->InternString("is_peak_rss_resettable")) {}

void ChromeSystemProbesParser::ParseProcessStats(int64_t ts, ConstBytes blob) {
  protos::pbzero::ProcessStats::Decoder stats(blob);
  for (auto it = stats.processes(); it; ++it) {
    protozero::ProtoDecoder proc(*it);
    auto pid_field =
        proc.FindField(protos::pbzero::ProcessStats::Process::kPidFieldNumber);
    uint32_t pid = pid_field.as_uint32();

    for (auto fld = proc.ReadField(); fld.valid(); fld = proc.ReadField()) {
      using ProcessStats = protos::pbzero::ProcessStats;
      if (fld.id() == ProcessStats::Process::kIsPeakRssResettableFieldNumber) {
        UniquePid upid = context_->process_tracker->GetOrCreateProcess(pid);
        context_->process_tracker->AddArgsToProcess(upid).AddArg(
            is_peak_rss_resettable_id_, Variadic::Boolean(fld.as_bool()));
        continue;
      }
      if (fld.id() ==
          ProcessStats::Process::kChromePrivateFootprintKbFieldNumber) {
        UniquePid upid = context_->process_tracker->GetOrCreateProcess(pid);
        TrackId track = context_->track_tracker->InternTrack(
            tracks::kChromeProcessStatsBlueprint,
            tracks::Dimensions(upid, "private_footprint_kb"));
        int64_t value = fld.as_int64() * 1024;
        context_->event_tracker->PushCounter(ts, static_cast<double>(value),
                                             track);
        continue;
      }
      if (fld.id() ==
          ProcessStats::Process::kChromePeakResidentSetKbFieldNumber) {
        UniquePid upid = context_->process_tracker->GetOrCreateProcess(pid);
        TrackId track = context_->track_tracker->InternTrack(
            tracks::kChromeProcessStatsBlueprint,
            tracks::Dimensions(upid, "peak_resident_set_kb"));
        int64_t value = fld.as_int64() * 1024;
        context_->event_tracker->PushCounter(ts, static_cast<double>(value),
                                             track);
        continue;
      }
    }
  }
}

}  // namespace perfetto::trace_processor
