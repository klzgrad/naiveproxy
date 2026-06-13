/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/trace_processor/importers/common/event_tracker.h"

#include <cstdint>
#include <optional>

#include "perfetto/ext/base/variant.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

EventTracker::EventTracker(TraceProcessorContext* context)
    : context_(context) {}

EventTracker::~EventTracker() = default;

void EventTracker::PushProcessCounterForThread(ProcessCounterForThread pcounter,
                                               int64_t timestamp,
                                               double value,
                                               UniqueTid utid) {
  const auto& counter = context_->storage->counter_table();
  auto opt_id = PushCounter(timestamp, value, kInvalidTrackId);
  if (opt_id) {
    PendingUpidResolutionCounter pending;
    pending.row = counter.FindById(*opt_id)->ToRowNumber().row_number();
    pending.utid = utid;
    pending.counter = pcounter;
    pending_upid_resolution_counter_.emplace_back(pending);
  }
}

std::optional<CounterId> EventTracker::PushCounter(int64_t timestamp,
                                                   double value,
                                                   TrackId track_id) {
  auto* counters = context_->storage->mutable_counter_table();
  return counters->Insert({timestamp, track_id, value, {}}).id;
}

std::optional<CounterId> EventTracker::PushCounter(
    int64_t timestamp,
    double value,
    TrackId track_id,
    const SetArgsCallback& args_callback) {
  auto maybe_counter_id = PushCounter(timestamp, value, track_id);
  if (maybe_counter_id) {
    ArgsTracker args_tracker(context_);
    auto inserter = args_tracker.AddArgsTo(*maybe_counter_id);
    args_callback(&inserter);
  }
  return maybe_counter_id;
}

void EventTracker::FlushPendingEvents() {
  const auto& thread_table = context_->storage->thread_table();
  for (const auto& pending_counter : pending_upid_resolution_counter_) {
    UniqueTid utid = pending_counter.utid;
    std::optional<UniquePid> upid = thread_table[utid].upid();

    // If we still don't know which process this thread belongs to, fall back
    // onto creating a thread counter track. It's too late to drop data
    // because the counter values have already been inserted.
    TrackId track_id;
    switch (pending_counter.counter.index()) {
      case base::variant_index<ProcessCounterForThread, OomScoreAdj>():
        if (upid.has_value()) {
          track_id = context_->track_tracker->InternTrack(
              tracks::kOomScoreAdjBlueprint, tracks::Dimensions(*upid));
        } else {
          track_id = context_->track_tracker->InternTrack(
              tracks::kOomScoreAdjThreadFallbackBlueprint,
              tracks::Dimensions(utid));
        }
        break;
      case base::variant_index<ProcessCounterForThread, MmEvent>(): {
        const auto& mm_event = std::get<MmEvent>(pending_counter.counter);
        if (upid.has_value()) {
          track_id = context_->track_tracker->InternTrack(
              tracks::kMmEventBlueprint,
              tracks::Dimensions(*upid, mm_event.type, mm_event.metric));
        } else {
          track_id = context_->track_tracker->InternTrack(
              tracks::kMmEventThreadFallbackBlueprint,
              tracks::Dimensions(utid, mm_event.type, mm_event.metric));
        }
        break;
      }
      case base::variant_index<ProcessCounterForThread, RssStat>(): {
        const auto& rss_stat = std::get<RssStat>(pending_counter.counter);
        if (upid.has_value()) {
          track_id = context_->track_tracker->InternTrack(
              tracks::kProcessMemoryBlueprint,
              tracks::Dimensions(*upid, rss_stat.process_memory_key));
        } else {
          track_id = context_->track_tracker->InternTrack(
              tracks::kProcessMemoryThreadFallbackBlueprint,
              tracks::Dimensions(utid, rss_stat.process_memory_key));
        }
        break;
      }
      case base::variant_index<ProcessCounterForThread, JsonCounter>(): {
        const auto& json = std::get<JsonCounter>(pending_counter.counter);
        if (upid.has_value()) {
          track_id = context_->track_tracker->InternTrack(
              tracks::kJsonCounterBlueprint,
              tracks::Dimensions(
                  *upid, context_->storage->GetString(json.counter_name_id)),
              tracks::DynamicName(json.counter_name_id));
        } else {
          track_id = context_->track_tracker->InternTrack(
              tracks::kJsonCounterThreadFallbackBlueprint,
              tracks::Dimensions(
                  utid, context_->storage->GetString(json.counter_name_id)),
              tracks::DynamicName(json.counter_name_id));
        }
        break;
      }
      case base::variant_index<ProcessCounterForThread, DmabufRssStat>(): {
        if (upid.has_value()) {
          track_id = context_->track_tracker->InternTrack(
              tracks::kProcessMemoryBlueprint,
              tracks::Dimensions(*upid, "dmabuf_rss"));
        } else {
          track_id = context_->track_tracker->InternTrack(
              tracks::kProcessMemoryThreadFallbackBlueprint,
              tracks::Dimensions(utid, "dmabuf_rss"));
        }
        break;
      }
    }
    auto& counter = *context_->storage->mutable_counter_table();
    counter[pending_counter.row].set_track_id(track_id);
  }
  pending_upid_resolution_counter_.clear();
}

}  // namespace perfetto::trace_processor
