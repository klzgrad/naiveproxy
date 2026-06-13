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

#include "src/trace_processor/importers/proto/graphics_frame_event_parser.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/trace/android/graphics_frame_event.pbzero.h"

namespace perfetto::trace_processor {

namespace {

constexpr char kQueueLostMessage[] =
    "Missing queue event. The slice is now a bit extended than it might "
    "actually have been";

constexpr auto kGraphicFrameEventBlueprint = tracks::SliceBlueprint(
    "graphics_frame_event",
    tracks::DimensionBlueprints(tracks::kNameFromTraceDimensionBlueprint),
    tracks::DynamicNameBlueprint());

}  // namespace

GraphicsFrameEventParser::GraphicsFrameEventParser(
    TraceProcessorContext* context)
    : context_(context),
      unknown_event_name_id_(context->storage->InternString("unknown_event")),
      no_layer_name_name_id_(context->storage->InternString("no_layer_name")),
      layer_name_key_id_(context->storage->InternString("layer_name")),
      queue_lost_message_id_(context->storage->InternString(kQueueLostMessage)),
      frame_number_id_(context->storage->InternString("frame_number")),
      queue_to_acquire_time_id_(
          context->storage->InternString("queue_to_acquire_time")),
      acquire_to_latch_time_id_(
          context->storage->InternString("acquire_to_latch_time")),
      latch_to_present_time_id_(
          context->storage->InternString("latch_to_present_time")),
      event_type_name_ids_{
          {context->storage->InternString(
               "unspecified_event") /* UNSPECIFIED */,
           context->storage->InternString("Dequeue") /* DEQUEUE */,
           context->storage->InternString("Queue") /* QUEUE */,
           context->storage->InternString("Post") /* POST */,
           context->storage->InternString(
               "AcquireFenceSignaled") /* ACQUIRE_FENCE */,
           context->storage->InternString("Latch") /* LATCH */,
           context->storage->InternString(
               "HWCCompositionQueued") /* HWC_COMPOSITION_QUEUED */,
           context->storage->InternString(
               "FallbackComposition") /* FALLBACK_COMPOSITION */,
           context->storage->InternString(
               "PresentFenceSignaled") /* PRESENT_FENCE */,
           context->storage->InternString(
               "ReleaseFenceSignaled") /* RELEASE_FENCE */,
           context->storage->InternString("Modify") /* MODIFY */,
           context->storage->InternString("Detach") /* DETACH */,
           context->storage->InternString("Attach") /* ATTACH */,
           context->storage->InternString("Cancel") /* CANCEL */}} {}

void GraphicsFrameEventParser::ParseGraphicsFrameEvent(int64_t timestamp,
                                                       ConstBytes blob) {
  protos::pbzero::GraphicsFrameEvent::Decoder frame_event(blob);
  if (!frame_event.has_buffer_event()) {
    return;
  }

  protos::pbzero::GraphicsFrameEvent::BufferEvent::Decoder event(
      frame_event.buffer_event());
  if (!event.has_buffer_id()) {
    context_->storage->IncrementStats(
        stats::graphics_frame_event_parser_errors);
    return;
  }

  // Use buffer id + layer name as key because sometimes the same buffer can be
  // used by different layers.
  StringId layer_name_id;
  StringId event_key;
  if (event.has_layer_name()) {
    layer_name_id = context_->storage->InternString(event.layer_name());
    base::StackString<1024> key_str("%u%.*s", event.buffer_id(),
                                    int(event.layer_name().size),
                                    event.layer_name().data);
    event_key = context_->storage->InternString(key_str.string_view());
  } else {
    layer_name_id = no_layer_name_name_id_;
    event_key = context_->storage->InternString(
        base::StackString<1024>("%u", event.buffer_id()).string_view());
  }

  CreateBufferEvent(timestamp, event, layer_name_id, event_key);
  CreatePhaseEvent(timestamp, event, layer_name_id, event_key);
}

void GraphicsFrameEventParser::CreateBufferEvent(
    int64_t timestamp,
    const GraphicsFrameEventDecoder& event,
    StringId layer_name_id,
    StringId event_key) {
  auto* it = buffer_event_map_.Insert(event_key, {}).first;
  switch (event.type()) {
    case GraphicsFrameEvent::DEQUEUE:
      break;
    case GraphicsFrameEvent::ACQUIRE_FENCE:
      it->acquire_ts = timestamp;
      break;
    case GraphicsFrameEvent::QUEUE:
      it->queue_ts = timestamp;
      break;
    case GraphicsFrameEvent::LATCH:
      it->latch_ts = timestamp;
      break;
    default:
      context_->storage->IncrementStats(
          stats::graphics_frame_event_parser_errors);
      break;
  }
  bool prev_is_dequeue = it->is_most_recent_dequeue_;
  it->is_most_recent_dequeue_ =
      event.type() ==
      protos::pbzero::GraphicsFrameEvent::BufferEventType::DEQUEUE;

  StringId event_name_id;
  if (event.has_type() &&
      static_cast<uint32_t>(event.type()) < event_type_name_ids_.size()) {
    event_name_id = event_type_name_ids_[static_cast<uint32_t>(event.type())];
  } else {
    event_name_id = unknown_event_name_id_;
  }

  base::StackString<4096> track_name("Buffer: %u %.*s", event.buffer_id(),
                                     int(event.layer_name().size),
                                     event.layer_name().data);
  TrackId track_id = context_->track_tracker->InternTrack(
      kGraphicFrameEventBlueprint, tracks::Dimensions(track_name.string_view()),
      tracks::DynamicName(
          context_->storage->InternString(track_name.string_view())));

  // Update the frame number for the previous dequeue event.
  uint32_t frame_number = event.has_frame_number() ? event.frame_number() : 0;
  if (event.type() == GraphicsFrameEvent::QUEUE && prev_is_dequeue) {
    context_->slice_tracker->AddArgs(
        track_id, kNullStringId, kNullStringId,
        [&](ArgsTracker::BoundInserter* inserter) {
          inserter->AddArg(frame_number_id_, Variadic::Integer(frame_number));
        });
  }

  const int64_t duration =
      event.has_duration_ns() ? static_cast<int64_t>(event.duration_ns()) : 0;
  context_->slice_tracker->Scoped(
      timestamp, track_id, kNullStringId, event_name_id, duration,
      [&](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(frame_number_id_, Variadic::Integer(frame_number));
        inserter->AddArg(layer_name_key_id_, Variadic::String(layer_name_id));
        inserter->AddArg(
            queue_to_acquire_time_id_,
            Variadic::Integer(std::max(it->acquire_ts - it->queue_ts,
                                       static_cast<int64_t>(0))));
        inserter->AddArg(acquire_to_latch_time_id_,
                         Variadic::Integer(it->latch_ts - it->acquire_ts));
        inserter->AddArg(latch_to_present_time_id_,
                         Variadic::Integer(timestamp - it->latch_ts));
      });
}

// Here we convert the buffer events into Phases(slices)
// APP: Dequeue to Queue
// Wait for GPU: Queue to Acquire
// SurfaceFlinger (SF): Latch to Present
// Display: Present to next Present (of the same layer)
void GraphicsFrameEventParser::CreatePhaseEvent(
    int64_t timestamp,
    const GraphicsFrameEventDecoder& event,
    StringId layer_name_id,
    StringId event_key) {
  auto* slices = context_->storage->mutable_slice_table();
  auto [it, inserted] = phase_event_map_.Insert(event_key, {});
  switch (event.type()) {
    case GraphicsFrameEvent::DEQUEUE: {
      if (auto* d = std::get_if<DequeueInfo>(&it->most_recent_event)) {
        // Error handling
        auto rr = d->slice_row.ToRowReference(slices);
        rr.set_name(context_->storage->InternString("0"));
        context_->slice_tracker->AddArgs(
            rr.track_id(), kNullStringId, kNullStringId,
            [&](ArgsTracker::BoundInserter* inserter) {
              inserter->AddArg(frame_number_id_, Variadic::Integer(0));
            });
        it->most_recent_event = std::monostate();
      }

      base::StackString<1024> track_name("APP_%u %.*s", event.buffer_id(),
                                         int(event.layer_name().size),
                                         event.layer_name().data);
      TrackId track_id = context_->track_tracker->InternTrack(
          kGraphicFrameEventBlueprint,
          tracks::Dimensions(track_name.string_view()),
          tracks::DynamicName(
              context_->storage->InternString(track_name.string_view())));
      auto res = InsertPhaseSlice(timestamp, event, track_id, layer_name_id);
      if (res) {
        it->most_recent_event = DequeueInfo{*res, timestamp};
      }
      break;
    }
    case GraphicsFrameEvent::QUEUE: {
      if (auto* d = std::get_if<DequeueInfo>(&it->most_recent_event)) {
        auto slice_rr = d->slice_row.ToRowReference(slices);
        context_->slice_tracker->End(
            timestamp, slice_rr.track_id(), kNullStringId, kNullStringId,
            [&](ArgsTracker::BoundInserter* inserter) {
              inserter->AddArg(frame_number_id_,
                               Variadic::Integer(event.frame_number()));
            });

        // Set the name of the slice to be the frame number since dequeue did
        // not have a frame number at that time.
        slice_rr.set_name(context_->storage->InternString(
            std::to_string(event.frame_number())));

        // The AcquireFence might be signaled before receiving a QUEUE event
        // sometimes. In that case, we shouldn't start a slice.
        if (it->last_acquire_ts && *it->last_acquire_ts > d->timestamp) {
          it->most_recent_event = std::monostate();
          return;
        }
      }
      base::StackString<1024> track_name("GPU_%u %.*s", event.buffer_id(),
                                         int(event.layer_name().size),
                                         event.layer_name().data);
      StringId track_name_id =
          context_->storage->InternString(track_name.string_view());
      TrackId track_id = context_->track_tracker->InternTrack(
          kGraphicFrameEventBlueprint,
          tracks::Dimensions(track_name.string_view()),
          tracks::DynamicName(track_name_id));
      InsertPhaseSlice(timestamp, event, track_id, layer_name_id);
      it->most_recent_event = QueueInfo{track_id};
      break;
    }
    case GraphicsFrameEvent::ACQUIRE_FENCE: {
      if (auto* q = std::get_if<QueueInfo>(&it->most_recent_event)) {
        context_->slice_tracker->End(timestamp, q->track);
        it->most_recent_event = std::monostate();
      }
      it->last_acquire_ts = timestamp;
      break;
    }
    case GraphicsFrameEvent::LATCH: {
      // b/157578286 - Sometimes Queue event goes missing. To prevent having a
      // wrong slice info, we try to close any existing APP slice.
      if (auto* d = std::get_if<DequeueInfo>(&it->most_recent_event)) {
        auto rr = d->slice_row.ToRowReference(slices);
        rr.set_name(context_->storage->InternString("0"));
        context_->slice_tracker->AddArgs(
            rr.track_id(), kNullStringId, kNullStringId,
            [&](ArgsTracker::BoundInserter* inserter) {
              inserter->AddArg(frame_number_id_, Variadic::Integer(0));
            });
      }
      base::StackString<1024> track_name("SF_%u %.*s", event.buffer_id(),
                                         int(event.layer_name().size),
                                         event.layer_name().data);
      TrackId track_id = context_->track_tracker->InternTrack(
          kGraphicFrameEventBlueprint,
          tracks::Dimensions(track_name.string_view()),
          tracks::DynamicName(
              context_->storage->InternString(track_name.string_view())));
      InsertPhaseSlice(timestamp, event, track_id, layer_name_id);
      it->most_recent_event = LatchInfo{track_id};
      break;
    }
    case GraphicsFrameEvent::PRESENT_FENCE: {
      if (auto* l = std::get_if<LatchInfo>(&it->most_recent_event)) {
        context_->slice_tracker->End(timestamp, l->track);
        it->most_recent_event = std::monostate();
      }
      auto [d_it, d_inserted] = display_map_.Insert(layer_name_id, {});
      if (d_it) {
        context_->slice_tracker->End(timestamp, *d_it);
      }
      base::StackString<1024> track_name("Display_%.*s",
                                         int(event.layer_name().size),
                                         event.layer_name().data);
      TrackId track_id = context_->track_tracker->InternTrack(
          kGraphicFrameEventBlueprint,
          tracks::Dimensions(track_name.string_view()),
          tracks::DynamicName(
              context_->storage->InternString(track_name.string_view())));
      InsertPhaseSlice(timestamp, event, track_id, layer_name_id);
      *d_it = track_id;
      break;
    }
    default:
      break;
  }
}

std::optional<GraphicsFrameEventParser::SliceRowNumber>
GraphicsFrameEventParser::InsertPhaseSlice(
    int64_t timestamp,
    const GraphicsFrameEventDecoder& event,
    TrackId track_id,
    StringId layer_name_id) {
  // If the frame_number is known, set it as the name of the slice.
  // If not known (DEQUEUE), set the name as the timestamp.
  // Timestamp is chosen here to ensure unique names for slices.
  StringId slice_name;
  if (event.frame_number() != 0) {
    slice_name =
        context_->storage->InternString(std::to_string(event.frame_number()));
  } else {
    slice_name = context_->storage->InternString(std::to_string(timestamp));
  }
  auto slice_id = context_->slice_tracker->Begin(
      timestamp, track_id, kNullStringId, slice_name,
      [&](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(frame_number_id_,
                         Variadic::Integer(event.frame_number()));
        inserter->AddArg(layer_name_key_id_, Variadic::String(layer_name_id));
      });
  if (slice_id) {
    return context_->storage->slice_table().FindById(*slice_id)->ToRowNumber();
  }
  return std::nullopt;
}

}  // namespace perfetto::trace_processor
