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

#include "src/trace_processor/importers/android_bugreport/android_battery_stats_reader.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>
#include <utility>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/string_view_splitter.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "src/trace_processor/importers/android_bugreport/android_battery_stats_history_string_tracker.h"
#include "src/trace_processor/importers/android_bugreport/android_dumpstate_event.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

namespace {

base::StatusOr<int64_t> StringToStatusOrInt64(base::StringView str) {
  std::optional<int64_t> possible_result = base::StringViewToInt64(str);
  if (!possible_result.has_value()) {
    return base::ErrStatus("Failed to convert string to int64_t");
  }
  return possible_result.value();
}
}  // namespace

AndroidBatteryStatsReader::AndroidBatteryStatsReader(
    TraceProcessorContext* context)
    : context_(context) {}

AndroidBatteryStatsReader::~AndroidBatteryStatsReader() = default;

base::Status AndroidBatteryStatsReader::ParseLine(base::StringView line) {
  base::StringViewSplitter splitter(line, ',');

  // consume the legacy version number which we expect to be at the start of
  // every line.
  if (splitter.NextToken() != "9") {
    return base::ErrStatus("Unexpected start of battery stats checkin line");
  }

  base::StringView possible_event_type = splitter.NextToken();

  if (possible_event_type == "hsp") {
    ASSIGN_OR_RETURN(int64_t index,
                     StringToStatusOrInt64(splitter.NextToken()));
    std::optional<int32_t> possible_uid =
        base::StringViewToInt32(splitter.NextToken());
    if (!possible_uid) {
      // This can happen if the bugreport is redacted incorrectly (i.e.
      // '[PHONE_NUMBER]').
      return base::OkStatus();
    }

    // the next element is quoted and can contain commas. Instead of
    // implementing general logic to parse quoted CSV elements just grab the
    // rest of the line, which is possible since this element should be the
    // last one on the line.
    base::StringView remainder = splitter.remainder();
    // remove the leading and trailing quotes from the hsp string
    size_t substr_start = remainder.find('"') + 1;
    size_t substr_end = remainder.rfind('"');
    base::StringView hsp_string =
        remainder.substr(substr_start, substr_end - substr_start);
    AndroidBatteryStatsHistoryStringTracker::GetOrCreate(context_)
        ->SetStringPoolItem(index, possible_uid.value(),
                            hsp_string.ToStdString());
  } else if (possible_event_type == "h") {
    const base::StringView time_adjustment_marker = ":TIME:";
    const base::StringView possible_timestamp = splitter.NextToken();
    size_t time_marker_index = possible_timestamp.find(time_adjustment_marker);
    if (time_marker_index != base::StringView::npos) {
      // Special case timestamp adjustment event.
      ASSIGN_OR_RETURN(current_timestamp_ms_,
                       StringToStatusOrInt64(possible_timestamp.substr(
                           time_marker_index + time_adjustment_marker.size())));
      return base::OkStatus();
    }
    if (possible_timestamp.find(":START") != base::StringView::npos) {
      // Ignore line
      return base::OkStatus();
    }
    if (possible_timestamp.find(":SHUTDOWN") != base::StringView::npos) {
      // Ignore line
      return base::OkStatus();
    }
    ASSIGN_OR_RETURN(int64_t parsed_timestamp_delta,
                     StringToStatusOrInt64(possible_timestamp));
    current_timestamp_ms_ += parsed_timestamp_delta;
    for (base::StringView item = splitter.NextToken(); !item.empty();
         item = splitter.NextToken()) {
      RETURN_IF_ERROR(ProcessBatteryStatsHistoryEvent(item));
    }

  } else if (possible_event_type == "0") {
    const base::StringView metadata_type = splitter.NextToken();
    if (metadata_type == "i") {
      const base::StringView info_type = splitter.NextToken();
      if (info_type == "vers") {
        ASSIGN_OR_RETURN(int64_t battery_stats_version,
                         StringToStatusOrInt64(splitter.NextToken()));
        AndroidBatteryStatsHistoryStringTracker::GetOrCreate(context_)
            ->battery_stats_version(
                static_cast<uint32_t>(battery_stats_version));
      }
    }
  } else {
    // TODO Implement UID parsing and other kinds of events.
  }

  return base::OkStatus();
}

base::Status AndroidBatteryStatsReader::ProcessBatteryStatsHistoryEvent(
    base::StringView raw_event) {
  AndroidDumpstateEvent event{
      AndroidDumpstateEvent::EventType::kBatteryStatsHistoryEvent,
      raw_event.ToStdString()};
  return SendToSorter(std::chrono::milliseconds(current_timestamp_ms_), event);
}

base::Status AndroidBatteryStatsReader::SendToSorter(
    std::chrono::nanoseconds event_ts,
    AndroidDumpstateEvent event) {
  ASSIGN_OR_RETURN(
      int64_t trace_ts,
      context_->clock_tracker->ToTraceTime(
          protos::pbzero::ClockSnapshot::Clock::REALTIME, event_ts.count()));
  context_->sorter->PushAndroidDumpstateEvent(trace_ts, std::move(event));
  return base::OkStatus();
}

void AndroidBatteryStatsReader::EndOfStream(base::StringView) {}

}  // namespace perfetto::trace_processor
