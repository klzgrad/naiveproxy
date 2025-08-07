/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/trace_processor/importers/common/clock_converter.h"

#include <time.h>

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <queue>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/hash.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"

namespace perfetto {
namespace trace_processor {

ClockConverter::ClockConverter(TraceProcessorContext* context)
    : context_(context) {}

void ClockConverter::MaybeInitialize() {
  if (is_initialized)
    return;

  is_initialized = true;
  timelines_.Insert(kRealClock, {});
  timelines_.Insert(kMonoClock, {});
  for (auto it = context_->storage->clock_snapshot_table().IterateRows(); it;
       ++it) {
    if (it.clock_id() == kRealClock || it.clock_id() == kMonoClock)
      timelines_.Find(it.clock_id())->emplace(it.ts(), it.clock_value());
  }
}

base::StatusOr<ClockConverter::Timestamp> ClockConverter::FromTraceTime(
    ClockId clock_id,
    Timestamp ts) {
  MaybeInitialize();

  Timeline* timeline = timelines_.Find(clock_id);
  if (!timeline) {
    return base::ErrStatus(
        "Provided clock has not been found in the converter "
        "clocks.");
  }

  if (timeline->empty()) {
    return base::ErrStatus("Target clock is not in the trace.");
  }

  auto next_snapshot = timeline->lower_bound(ts);

  // If lower bound was not found, it means that the ts was higher then the last
  // one. If that's the case we look for thhe last element and return clock
  // value for this + offset.
  if (next_snapshot == timeline->end()) {
    next_snapshot--;
    return next_snapshot->second + ts - next_snapshot->first;
  }

  // If there is a snapshot with this ts or lower bound is the first snapshot,
  // we have no other option then to return the clock value for this snapshot.
  if (next_snapshot == timeline->begin() || next_snapshot->first == ts)
    return next_snapshot->second;

  auto prev_snapshot = next_snapshot;
  prev_snapshot--;

  // The most truthful way to calculate the clock value is to use this formula,
  // as there is no reason to assume that the clock is monotonistic. This
  // prevents us from going back in time.
  return std::min(prev_snapshot->second + ts - prev_snapshot->first,
                  next_snapshot->second);
}

std::string ClockConverter::TimeToStr(Timestamp ts) {
  constexpr int64_t one_second_in_ns = 1LL * 1000LL * 1000LL * 1000LL;
  int64_t s = ts / one_second_in_ns;
  int64_t ns = ts % one_second_in_ns;

  time_t time_s = static_cast<time_t>(s);
  struct tm* time_tm = gmtime(&time_s);

  int seconds = time_tm->tm_sec;
  int minutes = time_tm->tm_min;
  int hours = time_tm->tm_hour;
  int day = time_tm->tm_mday;
  int month = time_tm->tm_mon + 1;
  int year = time_tm->tm_year + 1900;

  base::StackString<64> buf("%04d-%02d-%02dT%02d:%02d:%02d.%09" PRId64, year,
                            month, day, hours, minutes, seconds, ns);
  return buf.ToStdString();
}

}  // namespace trace_processor
}  // namespace perfetto
