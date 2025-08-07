/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "perfetto/tracing/track_event_state_tracker.h"

#include "perfetto/ext/base/hash.h"
#include "perfetto/tracing/internal/track_event_internal.h"

#include "protos/perfetto/common/interceptor_descriptor.gen.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pbzero.h"
#include "protos/perfetto/trace/track_event/process_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"

namespace perfetto {

using internal::TrackEventIncrementalState;

TrackEventStateTracker::~TrackEventStateTracker() = default;
TrackEventStateTracker::Delegate::~Delegate() = default;

// static
void TrackEventStateTracker::ProcessTracePacket(
    Delegate& delegate,
    SequenceState& sequence_state,
    const protos::pbzero::TracePacket_Decoder& packet) {
  UpdateIncrementalState(delegate, sequence_state, packet);

  if (!packet.has_track_event())
    return;
  perfetto::protos::pbzero::TrackEvent::Decoder track_event(
      packet.track_event());

  auto clock_id = packet.timestamp_clock_id();
  if (!packet.has_timestamp_clock_id())
    clock_id = sequence_state.default_clock_id;
  uint64_t timestamp = packet.timestamp();
  // TODO(mohitms): Incorporate unit multiplier as well.
  if (clock_id == internal::TrackEventIncrementalState::kClockIdIncremental) {
    timestamp += sequence_state.most_recent_absolute_time_ns;
    sequence_state.most_recent_absolute_time_ns = timestamp;
  }

  Track* track = &sequence_state.track;
  if (track_event.has_track_uuid()) {
    auto* session_state = delegate.GetSessionState();
    if (!session_state)
      return;  // Tracing must have ended.
    track = &session_state->tracks[track_event.track_uuid()];
  }

  // We only log the first category of each event.
  protozero::ConstChars category{};
  uint64_t category_iid = 0;
  if (auto iid_it = track_event.category_iids()) {
    category_iid = *iid_it;
    category.data = sequence_state.event_categories[category_iid].data();
    category.size = sequence_state.event_categories[category_iid].size();
  } else if (auto cat_it = track_event.categories()) {
    category.data = reinterpret_cast<const char*>(cat_it->data());
    category.size = cat_it->size();
  }

  protozero::ConstChars name{};
  uint64_t name_iid = track_event.name_iid();
  uint64_t name_hash = 0;
  uint64_t duration = 0;
  if (name_iid) {
    name.data = sequence_state.event_names[name_iid].data();
    name.size = sequence_state.event_names[name_iid].size();
  } else if (track_event.has_name()) {
    name.data = track_event.name().data;
    name.size = track_event.name().size;
  }

  if (name.data) {
    base::Hasher hash;
    hash.Update(name.data, name.size);
    name_hash = hash.digest();
  }

  size_t depth = track->stack.size();
  switch (track_event.type()) {
    case protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN: {
      StackFrame frame;
      frame.timestamp = timestamp;
      frame.name_hash = name_hash;
      if (track_event.has_track_uuid()) {
        frame.name = name.ToStdString();
        frame.category = category.ToStdString();
      } else {
        frame.name_iid = name_iid;
        frame.category_iid = category_iid;
      }
      track->stack.push_back(std::move(frame));
      break;
    }
    case protos::pbzero::TrackEvent::TYPE_SLICE_END:
      if (!track->stack.empty()) {
        const auto& prev_frame = track->stack.back();
        if (prev_frame.name_iid) {
          name.data = sequence_state.event_names[prev_frame.name_iid].data();
          name.size = sequence_state.event_names[prev_frame.name_iid].size();
        } else {
          name.data = prev_frame.name.data();
          name.size = prev_frame.name.size();
        }
        name_hash = prev_frame.name_hash;
        if (prev_frame.category_iid) {
          category.data =
              sequence_state.event_categories[prev_frame.category_iid].data();
          category.size =
              sequence_state.event_categories[prev_frame.category_iid].size();
        } else {
          category.data = prev_frame.category.data();
          category.size = prev_frame.category.size();
        }
        duration = timestamp - prev_frame.timestamp;
        depth--;
      }
      break;
    case protos::pbzero::TrackEvent::TYPE_INSTANT:
      break;
    case protos::pbzero::TrackEvent::TYPE_COUNTER:
    case protos::pbzero::TrackEvent::TYPE_UNSPECIFIED:
      // TODO(skyostil): Support counters.
      return;
  }

  ParsedTrackEvent parsed_event{track_event};
  parsed_event.timestamp_ns = timestamp;
  parsed_event.duration_ns = duration;
  parsed_event.stack_depth = depth;
  parsed_event.category = category;
  parsed_event.name = name;
  parsed_event.name_hash = name_hash;
  delegate.OnTrackEvent(*track, parsed_event);

  if (track_event.type() == protos::pbzero::TrackEvent::TYPE_SLICE_END &&
      !track->stack.empty()) {
    track->stack.pop_back();
  }
}

// static
void TrackEventStateTracker::UpdateIncrementalState(
    Delegate& delegate,
    SequenceState& sequence_state,
    const protos::pbzero::TracePacket_Decoder& packet) {
#if PERFETTO_DCHECK_IS_ON()
  if (!sequence_state.sequence_id) {
    sequence_state.sequence_id = packet.trusted_packet_sequence_id();
  } else {
    PERFETTO_DCHECK(sequence_state.sequence_id ==
                    packet.trusted_packet_sequence_id());
  }
#endif

  perfetto::protos::pbzero::ClockSnapshot::Decoder snapshot(
      packet.clock_snapshot());
  for (auto it = snapshot.clocks(); it; ++it) {
    perfetto::protos::pbzero::ClockSnapshot::Clock::Decoder clock(*it);
    // TODO(mohitms) : Handle the incremental clock other than default one.
    if (clock.is_incremental() &&
        clock.clock_id() ==
            internal::TrackEventIncrementalState::kClockIdIncremental) {
      sequence_state.most_recent_absolute_time_ns =
          clock.timestamp() * clock.unit_multiplier_ns();
      break;
    }
  }

  if (packet.sequence_flags() &
      perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED) {
    // Convert any existing event names and categories on the stack to
    // non-interned strings so we can look up their names even after the
    // incremental state is gone.
    for (auto& frame : sequence_state.track.stack) {
      if (frame.name_iid) {
        frame.name = sequence_state.event_names[frame.name_iid];
        frame.name_iid = 0u;
      }
      if (frame.category_iid) {
        frame.category = sequence_state.event_categories[frame.category_iid];
        frame.category_iid = 0u;
      }
    }
    sequence_state.event_names.clear();
    sequence_state.event_categories.clear();
    sequence_state.debug_annotation_names.clear();
    sequence_state.track.uuid = 0u;
    sequence_state.track.index = 0u;
  }
  if (packet.has_interned_data()) {
    perfetto::protos::pbzero::InternedData::Decoder interned_data(
        packet.interned_data());
    for (auto it = interned_data.event_names(); it; it++) {
      perfetto::protos::pbzero::EventName::Decoder entry(*it);
      sequence_state.event_names[entry.iid()] = entry.name().ToStdString();
    }
    for (auto it = interned_data.event_categories(); it; it++) {
      perfetto::protos::pbzero::EventCategory::Decoder entry(*it);
      sequence_state.event_categories[entry.iid()] = entry.name().ToStdString();
    }
    for (auto it = interned_data.debug_annotation_names(); it; it++) {
      perfetto::protos::pbzero::DebugAnnotationName::Decoder entry(*it);
      sequence_state.debug_annotation_names[entry.iid()] =
          entry.name().ToStdString();
    }
  }
  if (packet.has_trace_packet_defaults()) {
    perfetto::protos::pbzero::TracePacketDefaults::Decoder defaults(
        packet.trace_packet_defaults());
    if (defaults.has_track_event_defaults()) {
      perfetto::protos::pbzero::TrackEventDefaults::Decoder
          track_event_defaults(defaults.track_event_defaults());
      sequence_state.track.uuid = track_event_defaults.track_uuid();
      if (defaults.has_timestamp_clock_id())
        sequence_state.default_clock_id = defaults.timestamp_clock_id();
    }
  }
  if (packet.has_track_descriptor()) {
    perfetto::protos::pbzero::TrackDescriptor::Decoder track_descriptor(
        packet.track_descriptor());
    auto* session_state = delegate.GetSessionState();
    auto& track = session_state->tracks[track_descriptor.uuid()];
    if (!track.index)
      track.index = static_cast<uint32_t>(session_state->tracks.size() + 1);
    track.uuid = track_descriptor.uuid();

    if (track_descriptor.has_name()) {
      track.name = track_descriptor.name().ToStdString();
    } else if (track_descriptor.has_static_name()) {
      track.name = track_descriptor.static_name().ToStdString();
    }
    track.pid = 0;
    track.tid = 0;
    if (track_descriptor.has_process()) {
      perfetto::protos::pbzero::ProcessDescriptor::Decoder process(
          track_descriptor.process());
      track.pid = process.pid();
      if (track.name.empty())
        track.name = process.process_name().ToStdString();
    } else if (track_descriptor.has_thread()) {
      perfetto::protos::pbzero::ThreadDescriptor::Decoder thread(
          track_descriptor.thread());
      track.pid = thread.pid();
      track.tid = thread.tid();
      if (track.name.empty())
        track.name = thread.thread_name().ToStdString();
    }
    delegate.OnTrackUpdated(track);

    // Mirror properties to the default track of the sequence. Note that
    // this does not catch updates to the default track written through other
    // sequences.
    if (track.uuid == sequence_state.track.uuid) {
      sequence_state.track.index = track.index;
      sequence_state.track.name = track.name;
      sequence_state.track.pid = track.pid;
      sequence_state.track.tid = track.tid;
      sequence_state.track.user_data = track.user_data;
    }
  }
}

TrackEventStateTracker::ParsedTrackEvent::ParsedTrackEvent(
    const perfetto::protos::pbzero::TrackEvent::Decoder& track_event_)
    : track_event(track_event_) {}

}  // namespace perfetto
