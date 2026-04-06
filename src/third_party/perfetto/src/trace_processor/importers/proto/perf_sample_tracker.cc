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
#include "src/trace_processor/importers/proto/perf_sample_tracker.h"

#include <cinttypes>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/perfetto/common/perf_events.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_packet.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

namespace {
// Follow perf tool naming convention.
const char* StringifyCounter(int32_t counter) {
  using protos::pbzero::PerfEvents;
  switch (counter) {
    // software:
    case PerfEvents::SW_CPU_CLOCK:
      return "cpu-clock";
    case PerfEvents::SW_PAGE_FAULTS:
      return "page-faults";
    case PerfEvents::SW_TASK_CLOCK:
      return "task-clock";
    case PerfEvents::SW_CONTEXT_SWITCHES:
      return "context-switches";
    case PerfEvents::SW_CPU_MIGRATIONS:
      return "cpu-migrations";
    case PerfEvents::SW_PAGE_FAULTS_MIN:
      return "minor-faults";
    case PerfEvents::SW_PAGE_FAULTS_MAJ:
      return "major-faults";
    case PerfEvents::SW_ALIGNMENT_FAULTS:
      return "alignment-faults";
    case PerfEvents::SW_EMULATION_FAULTS:
      return "emulation-faults";
    case PerfEvents::SW_DUMMY:
      return "dummy";
    // hardware:
    case PerfEvents::HW_CPU_CYCLES:
      return "cpu-cycles";
    case PerfEvents::HW_INSTRUCTIONS:
      return "instructions";
    case PerfEvents::HW_CACHE_REFERENCES:
      return "cache-references";
    case PerfEvents::HW_CACHE_MISSES:
      return "cache-misses";
    case PerfEvents::HW_BRANCH_INSTRUCTIONS:
      return "branch-instructions";
    case PerfEvents::HW_BRANCH_MISSES:
      return "branch-misses";
    case PerfEvents::HW_BUS_CYCLES:
      return "bus-cycles";
    case PerfEvents::HW_STALLED_CYCLES_FRONTEND:
      return "stalled-cycles-frontend";
    case PerfEvents::HW_STALLED_CYCLES_BACKEND:
      return "stalled-cycles-backend";
    case PerfEvents::HW_REF_CPU_CYCLES:
      return "ref-cycles";

    default:
      break;
  }
  PERFETTO_DLOG("Unknown PerfEvents::Counter enum value");
  return "unknown";
}

template <typename T>
StringId InternCounterName(const T& event, TraceProcessorContext* context) {
  using protos::pbzero::PerfEvents;
  auto base_counter_name = [&]() -> base::StringView {
    // explicit name from config takes precedence
    if (event.name().size > 0) {
      return event.name();
    }
    if (event.has_counter()) {
      return StringifyCounter(event.counter());
    }
    if (event.has_tracepoint()) {
      PerfEvents::Tracepoint::Decoder tracepoint(event.tracepoint());
      return tracepoint.name();
    }
    if (event.has_raw_event()) {
      PerfEvents::RawEvent::Decoder raw(event.raw_event());
      // This doesn't follow any pre-existing naming scheme, but aims to be a
      // short-enough default that is distinguishable.
      base::StackString<128> name(
          "raw.0x%" PRIx32 ".0x%" PRIx64 ".0x%" PRIx64 ".0x%" PRIx64,
          raw.type(), raw.config(), raw.config1(), raw.config2());
      return name.string_view();
    }
    PERFETTO_DLOG("Could not name the perf counter");
    return "unknown";
  };

  std::string name = base_counter_name().ToStdString();

  // Suffix with event modifiers, if any. Following the perftool convention.
  std::string modifiers;
  for (auto it = event.modifiers(); it; ++it) {
    if (it->as_int32() == PerfEvents::EVENT_MODIFIER_COUNT_USERSPACE) {
      modifiers += 'u';
    }
    if (it->as_int32() == PerfEvents::EVENT_MODIFIER_COUNT_KERNEL) {
      modifiers += 'k';
    }
    if (it->as_int32() == PerfEvents::EVENT_MODIFIER_COUNT_HYPERVISOR) {
      modifiers += 'h';
    }
  }
  if (!modifiers.empty()) {
    name = name + ':' + modifiers;
  }
  return context->storage->InternString(name);
}

}  // namespace

PerfSampleTracker::PerfSampleTracker(TraceProcessorContext* context)
    : is_timebase_id_(context->storage->InternString("is_timebase")),
      context_(context) {}

PerfSampleTracker::SamplingStreamInfo PerfSampleTracker::GetSamplingStreamInfo(
    uint32_t seq_id,
    uint32_t cpu,
    protos::pbzero::TracePacketDefaults::Decoder* nullable_defaults) {
  using protos::pbzero::FollowerEvent;
  using protos::pbzero::PerfEvents;
  using protos::pbzero::PerfSampleDefaults;

  auto seq_it = seq_state_.find(seq_id);
  if (seq_it == seq_state_.end()) {
    seq_it = seq_state_.emplace(seq_id, CreatePerfSession()).first;
  }
  SequenceState* seq_state = &seq_it->second;
  tables::PerfSessionTable::Id session_id = seq_state->perf_session_id;

  auto cpu_it = seq_state->per_cpu.find(cpu);
  if (cpu_it != seq_state->per_cpu.end())
    return {seq_state->perf_session_id, cpu_it->second.timebase_track_id,
            cpu_it->second.follower_track_ids};

  std::optional<PerfSampleDefaults::Decoder> perf_defaults;
  if (nullable_defaults && nullable_defaults->has_perf_sample_defaults()) {
    perf_defaults.emplace(nullable_defaults->perf_sample_defaults());
  }

  StringId name_id = kNullStringId;
  if (perf_defaults.has_value()) {
    PerfEvents::Timebase::Decoder timebase(perf_defaults->timebase());
    name_id = InternCounterName(timebase, context_);
  } else {
    // No defaults means legacy producer implementation, assume default timebase
    // of per-cpu timer. This means either an Android R or early S build.
    name_id = context_->storage->InternString(
        StringifyCounter(protos::pbzero::PerfEvents::SW_CPU_CLOCK));
  }

  base::StringView name = context_->storage->GetString(name_id);
  TrackId timebase_track_id = context_->track_tracker->InternTrack(
      tracks::kPerfCpuCounterBlueprint,
      tracks::Dimensions(cpu, session_id.value, name),
      tracks::DynamicName(name_id),
      [this](ArgsTracker::BoundInserter& inserter) {
        inserter.AddArg(is_timebase_id_, Variadic::Boolean(true));
      });

  std::vector<TrackId> follower_track_ids;
  if (perf_defaults.has_value()) {
    for (auto it = perf_defaults->followers(); it; ++it) {
      FollowerEvent::Decoder follower(*it);
      StringId follower_name_id = InternCounterName(follower, context_);
      base::StringView follower_name =
          context_->storage->GetString(follower_name_id);
      follower_track_ids.push_back(context_->track_tracker->InternTrack(
          tracks::kPerfCpuCounterBlueprint,
          tracks::Dimensions(cpu, session_id.value, follower_name),
          tracks::DynamicName(follower_name_id),
          [this](ArgsTracker::BoundInserter& inserter) {
            inserter.AddArg(is_timebase_id_, Variadic::Boolean(false));
          }));
    }
  }

  seq_state->per_cpu.emplace(
      cpu, CpuSequenceState{timebase_track_id, follower_track_ids});

  // If the config requested process sharding, record in the stats table which
  // shard was chosen for the trace. It should be the same choice for all data
  // sources within one trace, but for consistency with other stats we put an
  // entry per data source (i.e. |perf_session_id|, not to be confused with the
  // tracing session).
  if (perf_defaults.has_value() && perf_defaults->process_shard_count() > 0) {
    context_->storage->SetIndexedStats(
        stats::perf_process_shard_count, static_cast<int>(session_id.value),
        static_cast<int64_t>(perf_defaults->process_shard_count()));
    context_->storage->SetIndexedStats(
        stats::perf_chosen_process_shard, static_cast<int>(session_id.value),
        static_cast<int64_t>(perf_defaults->chosen_process_shard()));
  }

  return {session_id, timebase_track_id, std::move(follower_track_ids)};
}

tables::PerfSessionTable::Id PerfSampleTracker::CreatePerfSession() {
  return context_->storage->mutable_perf_session_table()->Insert({}).id;
}

}  // namespace perfetto::trace_processor
