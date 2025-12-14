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

#include "src/trace_processor/importers/perf/perf_event_attr.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <optional>

#include "perfetto/ext/base/string_view.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/perf/perf_counter.h"
#include "src/trace_processor/importers/perf/perf_event.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/tables/track_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor::perf_importer {

namespace {

constexpr auto kBytesPerField = 8;

size_t CountSetFlags(uint64_t sample_type) {
  return static_cast<size_t>(PERFETTO_POPCOUNT(sample_type));
}

std::optional<size_t> TimeOffsetFromEndOfNonSampleRecord(
    const perf_event_attr& attr) {
  constexpr uint64_t kFlagsFromTimeToEnd =
      PERF_SAMPLE_TIME | PERF_SAMPLE_ID | PERF_SAMPLE_STREAM_ID |
      PERF_SAMPLE_CPU | PERF_SAMPLE_IDENTIFIER;
  if (!attr.sample_id_all || !(attr.sample_type & PERF_SAMPLE_TIME)) {
    return std::nullopt;
  }
  return CountSetFlags(attr.sample_type & kFlagsFromTimeToEnd) * kBytesPerField;
}

std::optional<size_t> TimeOffsetFromStartOfSampleRecord(
    const perf_event_attr& attr) {
  constexpr uint64_t kFlagsFromStartToTime =
      PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_IP | PERF_SAMPLE_TID;
  if (!(attr.sample_type & PERF_SAMPLE_TIME)) {
    return std::nullopt;
  }
  return CountSetFlags(attr.sample_type & kFlagsFromStartToTime) *
         kBytesPerField;
}

std::optional<size_t> IdOffsetFromStartOfSampleRecord(
    const perf_event_attr& attr) {
  constexpr uint64_t kFlagsFromStartToId = PERF_SAMPLE_IDENTIFIER |
                                           PERF_SAMPLE_IP | PERF_SAMPLE_TID |
                                           PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR;

  if (attr.sample_type & PERF_SAMPLE_IDENTIFIER) {
    return 0;
  }

  if (attr.sample_type & PERF_SAMPLE_ID) {
    return CountSetFlags(attr.sample_type & kFlagsFromStartToId) *
           kBytesPerField;
  }
  return std::nullopt;
}

std::optional<size_t> IdOffsetFromEndOfNonSampleRecord(
    const perf_event_attr& attr) {
  constexpr uint64_t kFlagsFromIdToEnd =
      PERF_SAMPLE_ID | PERF_SAMPLE_STREAM_ID | PERF_SAMPLE_CPU |
      PERF_SAMPLE_IDENTIFIER;

  if (attr.sample_type & PERF_SAMPLE_IDENTIFIER) {
    return kBytesPerField;
  }

  if (attr.sample_type & PERF_SAMPLE_ID) {
    return CountSetFlags(attr.sample_type & kFlagsFromIdToEnd) * kBytesPerField;
  }

  return std::nullopt;
}

size_t GetSampleIdSize(const perf_event_attr& attr) {
  constexpr uint64_t kSampleIdFlags = PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
                                      PERF_SAMPLE_ID | PERF_SAMPLE_STREAM_ID |
                                      PERF_SAMPLE_CPU | PERF_SAMPLE_IDENTIFIER;
  return CountSetFlags(attr.sample_type & kSampleIdFlags) * kBytesPerField;
}

ClockTracker::ClockId ExtractClockId(const perf_event_attr& attr) {
  if (!attr.use_clockid) {
    return protos::pbzero::BUILTIN_CLOCK_PERF;
  }
  switch (attr.clockid) {
    // Linux perf uses the values in <time.h> not sure if these are portable
    // across platforms, so using the actual values here just in case.
    case 0:  // CLOCK_REALTIME
      return protos::pbzero::BUILTIN_CLOCK_REALTIME;
    case 1:  // CLOCK_MONOTONIC
      return protos::pbzero::BUILTIN_CLOCK_MONOTONIC;
    case 4:  // CLOCK_MONOTONIC_RAW
      return protos::pbzero::BUILTIN_CLOCK_MONOTONIC_RAW;
    case 5:  // CLOCK_REALTIME_COARSE
      return protos::pbzero::BUILTIN_CLOCK_REALTIME_COARSE;
    case 6:  // CLOCK_MONOTONIC_COARSE
      return protos::pbzero::BUILTIN_CLOCK_MONOTONIC_COARSE;
    case 7:  // CLOCK_BOOTTIME
      return protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
    default:
      return protos::pbzero::BUILTIN_CLOCK_UNKNOWN;
  }
}
}  // namespace

PerfEventAttr::PerfEventAttr(TraceProcessorContext* context,
                             tables::PerfSessionTable::Id perf_session_id,
                             perf_event_attr attr)
    : context_(context),
      clock_id_(ExtractClockId(attr)),
      perf_session_id_(perf_session_id),
      attr_(attr),
      time_offset_from_start_(TimeOffsetFromStartOfSampleRecord(attr_)),
      time_offset_from_end_(TimeOffsetFromEndOfNonSampleRecord(attr_)),
      id_offset_from_start_(IdOffsetFromStartOfSampleRecord(attr_)),
      id_offset_from_end_(IdOffsetFromEndOfNonSampleRecord(attr_)),
      sample_id_size_(GetSampleIdSize(attr_)) {}

PerfEventAttr::~PerfEventAttr() = default;

PerfCounter& PerfEventAttr::GetOrCreateCounter(std::optional<uint32_t> cpu) {
  if (!cpu) {
    if (!global_counter_) {
      global_counter_ = std::make_unique<PerfCounter>(CreateGlobalCounter());
    }
    return *global_counter_;
  }
  auto it = counters_.find(*cpu);
  if (it == counters_.end()) {
    it = counters_.emplace(*cpu, CreateCpuCounter(*cpu)).first;
  }
  return it->second;
}

PerfCounter PerfEventAttr::CreateGlobalCounter() const {
  base::StringView name(event_name_);
  TrackId track_id = context_->track_tracker->InternTrack(
      tracks::kPerfGlobalCounterBlueprint,
      tracks::Dimensions(perf_session_id_.value, name),
      tracks::DynamicName(context_->storage->InternString(name)),
      [this](ArgsTracker::BoundInserter& inserter) {
        inserter.AddArg(context_->storage->InternString("is_timebase"),
                        Variadic::Boolean(is_timebase()));
      });
  return {context_->storage->mutable_counter_table(), track_id, is_timebase()};
}

PerfCounter PerfEventAttr::CreateCpuCounter(uint32_t cpu) const {
  base::StringView name(event_name_);
  TrackId track_id = context_->track_tracker->InternTrack(
      tracks::kPerfCpuCounterBlueprint,
      tracks::Dimensions(cpu, perf_session_id_.value, name),
      tracks::DynamicName(context_->storage->InternString(name)),
      [this](ArgsTracker::BoundInserter& inserter) {
        inserter.AddArg(context_->storage->InternString("is_timebase"),
                        Variadic::Boolean(is_timebase()));
      });
  return {context_->storage->mutable_counter_table(), track_id, is_timebase()};
}

}  // namespace perfetto::trace_processor::perf_importer
