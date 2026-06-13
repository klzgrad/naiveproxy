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

#include "src/trace_processor/importers/gecko/gecko_trace_parser.h"

#include <cstdint>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/gecko/gecko_event.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/json_args.h"
#include "src/trace_processor/util/json_parser.h"

namespace perfetto::trace_processor::gecko_importer {

namespace {

template <typename T>
constexpr uint32_t GeckoOneOf() {
  return base::variant_index<GeckoEvent::OneOf, T>();
}

// Blueprint for Firefox marker tracks. Dimensioning by
// `(utid, category, name)` means every distinct marker name within a category
// on every thread gets its own track — matching the Firefox Profiler's marker
// chart layout, where the chart is structured as thread → category → name.
//
// The Firefox profiler format does not give us any way to disambiguate two
// same-name IntervalStart markers happening concurrently (there is no
// per-marker ID), and same-name complete Intervals overlapping is unheard of
// in practice, so we treat each (utid, category, name) as a single track and
// let the slice tracker's nesting check reject the rare malformed input.
constexpr auto kFirefoxMarkerBlueprint = tracks::SliceBlueprint(
    "firefox_marker",
    tracks::DimensionBlueprints(
        tracks::kThreadDimensionBlueprint,
        tracks::StringIdDimensionBlueprint("firefox_marker_category"),
        tracks::StringIdDimensionBlueprint("firefox_marker_name")),
    tracks::DynamicNameBlueprint());

}  // namespace

GeckoTraceParser::GeckoTraceParser(TraceProcessorContext* context)
    : context_(context) {}

GeckoTraceParser::~GeckoTraceParser() = default;

void GeckoTraceParser::ParseThreadMetadata(
    const GeckoEvent::ThreadMetadata& thread) {
  UniqueTid utid =
      context_->process_tracker->UpdateThread(thread.tid, thread.pid);
  context_->process_tracker->UpdateThreadName(utid, thread.name,
                                              ThreadNamePriority::kOther);
}

void GeckoTraceParser::ParseStackSample(int64_t ts,
                                        const GeckoEvent::StackSample& sample) {
  auto* ss = context_->storage->mutable_cpu_profile_stack_sample_table();
  tables::CpuProfileStackSampleTable::Row row;
  row.ts = ts;
  row.callsite_id = sample.callsite_id;
  row.utid = context_->process_tracker->GetOrCreateThread(sample.tid);
  ss->Insert(row);
}

void GeckoTraceParser::ParseMarker(int64_t ts,
                                   const GeckoEvent::Marker& marker) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(marker.tid);

  // Flattens the marker `data` payload into args under the `data` namespace,
  // e.g. `{"type": "DOMEvent", "eventType": "mousedown"}` becomes
  // `data.type=DOMEvent` and `data.eventType=mousedown`.
  json::Iterator data_iterator;
  auto args_inserter = [&](ArgsTracker::BoundInserter* inserter) {
    if (marker.data_json_size == 0) {
      return;
    }
    json::AddJsonValueToArgs(data_iterator, marker.data_json.get(),
                             marker.data_json.get() + marker.data_json_size,
                             /*flat_key=*/"data", /*key=*/"data",
                             context_->storage.get(), inserter);
  };

  // One stable track per (utid, category, marker name); same-name markers all
  // land here and are nested by the slice tracker's LIFO matching.
  TrackId track_id = context_->track_tracker->InternTrack(
      kFirefoxMarkerBlueprint,
      tracks::Dimensions(utid, marker.category, marker.name), marker.name);

  SliceTracker* slice_tracker = context_->slice_tracker.get();
  using MarkerPhase = GeckoEvent::MarkerPhase;
  switch (marker.phase) {
    case MarkerPhase::kInstant:
      slice_tracker->Scoped(ts, track_id, marker.category, marker.name,
                            /*duration=*/0, args_inserter);
      break;
    case MarkerPhase::kInterval:
      slice_tracker->Scoped(ts, track_id, marker.category, marker.name,
                            marker.dur, args_inserter);
      break;
    case MarkerPhase::kIntervalStart:
      slice_tracker->Begin(ts, track_id, marker.category, marker.name,
                           args_inserter);
      break;
    case MarkerPhase::kIntervalEnd:
      // LIFO-matched by name on this track.
      slice_tracker->End(ts, track_id, marker.category, marker.name,
                         args_inserter);
      break;
  }
}

void GeckoTraceParser::Parse(int64_t ts, GeckoEvent evt) {
  switch (evt.oneof.index()) {
    case GeckoOneOf<GeckoEvent::ThreadMetadata>():
      ParseThreadMetadata(std::get<GeckoEvent::ThreadMetadata>(evt.oneof));
      break;
    case GeckoOneOf<GeckoEvent::StackSample>():
      ParseStackSample(ts, std::get<GeckoEvent::StackSample>(evt.oneof));
      break;
    case GeckoOneOf<GeckoEvent::Marker>():
      ParseMarker(ts, std::get<GeckoEvent::Marker>(evt.oneof));
      break;
  }
}

}  // namespace perfetto::trace_processor::gecko_importer
