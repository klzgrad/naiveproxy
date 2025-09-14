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

#include "src/trace_processor/importers/android_bugreport/android_log_reader.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <utility>

#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/status_macros.h"
#include "protos/perfetto/common/android_log_constants.pbzero.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "src/trace_processor/importers/android_bugreport/android_log_event.h"
#include "src/trace_processor/importers/common/clock_converter.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

namespace {

// Reads a base-10 number and advances the passed StringView beyond the *last*
// instance of `sep`. Example:
// Input:  it="1234   bar".
// Output: it="bar", ret=1234.
//
// `decimal_scale` is used to parse decimals and defines the output resolution.
// E.g. input="1",    decimal_scale=1000 -> res=100
//      input="12",   decimal_scale=1000 -> res=120
//      input="123",  decimal_scale=1000 -> res=123
//      input="1234", decimal_scale=1000 -> res=123
//      input="1234", decimal_scale=1000000 -> res=123400
std::optional<int> ReadNumAndAdvance(base::StringView* it,
                                     char sep,
                                     int decimal_scale = 0) {
  int num = 0;
  bool sep_found = false;
  size_t next_it = 0;
  bool invalid_chars_found = false;
  for (size_t i = 0; i < it->size(); i++) {
    char c = it->at(i);
    if (c == sep) {
      next_it = i + 1;
      sep_found = true;
      continue;
    }
    if (sep_found)
      break;
    if (c >= '0' && c <= '9') {
      int digit = static_cast<int>(c - '0');
      if (!decimal_scale) {
        num = num * 10 + digit;
      } else {
        decimal_scale /= 10;
        num += digit * decimal_scale;
      }
      continue;
    }
    // We found something that is not a digit. Keep looking for the next `sep`
    // but flag the current token as invalid.
    invalid_chars_found = true;
  }
  if (!sep_found)
    return std::nullopt;
  // If we find non-digit characters, we want to still skip the token but return
  // std::nullopt. The parser below relies on token skipping to deal with cases
  // where the uid (which we don't care about) is literal ("root" rather than
  // 0).
  *it = it->substr(next_it);
  if (invalid_chars_found)
    return std::nullopt;
  return num;
}

int32_t ToYear(std::chrono::seconds epoch) {
  time_t time = static_cast<time_t>(epoch.count());
  auto* time_tm = gmtime(&time);
  return time_tm->tm_year + 1900;
}

int32_t GetCurrentYear() {
  return ToYear(base::GetWallTimeS());
}

int32_t GuessYear(TraceProcessorContext* context) {
  if (context->sorter->max_timestamp() == 0) {
    return GetCurrentYear();
  }
  auto time =
      context->clock_converter->ToRealtime(context->sorter->max_timestamp());
  if (!time.ok()) {
    return GetCurrentYear();
  }
  std::chrono::nanoseconds ns(*time);
  return ToYear(std::chrono::duration_cast<std::chrono::seconds>(ns));
}

}  // namespace
AndroidLogReader::AndroidLogReader(TraceProcessorContext* context)
    : AndroidLogReader(context, GuessYear(context)) {}

AndroidLogReader::AndroidLogReader(TraceProcessorContext* context,
                                   int32_t year,
                                   bool wait_for_tz)
    : context_(context), year_(year), wait_for_tz_(wait_for_tz) {}

AndroidLogReader::~AndroidLogReader() = default;

base::Status AndroidLogReader::ParseLine(base::StringView line) {
  if (line.size() < 30 ||
      (line.at(0) == '-' && line.at(1) == '-' && line.at(2) == '-')) {
    // These are markers like "--------- switch to radio" which we ignore.
    // The smallest valid logcat line has around 30 chars, as follows:
    // "06-24 23:10:00.123  1 1 D : ..."
    return base::OkStatus();
  }

  if (!format_.has_value()) {
    format_ = AndroidLogEvent::DetectFormat(line);
    if (!format_.has_value()) {
      PERFETTO_DLOG("Could not detect logcat format for: |%s|",
                    line.ToStdString().c_str());
      context_->storage->IncrementStats(stats::android_log_format_invalid);
      return base::OkStatus();
    }
  }

  base::StringView it = line;
  // 06-24 16:24:23.441532 23153 23153 I wm_on_stop_called: message ...
  // 07-28 14:25:13.506  root     0     0 I x86/fpu : Supporting XSAVE feature
  // 0x002: 'SSE registers'
  std::optional<int> month = ReadNumAndAdvance(&it, '-');
  std::optional<int> day = ReadNumAndAdvance(&it, ' ');
  std::optional<int> hour = ReadNumAndAdvance(&it, ':');
  std::optional<int> minute = ReadNumAndAdvance(&it, ':');
  std::optional<int> sec = ReadNumAndAdvance(&it, '.');
  std::optional<int> ns = ReadNumAndAdvance(&it, ' ', 1000 * 1000 * 1000);

  if (format_ == AndroidLogEvent::Format::kBugreport)
    ReadNumAndAdvance(&it, ' ');  // Skip the UID column.

  std::optional<int> pid = ReadNumAndAdvance(&it, ' ');
  std::optional<int> tid = ReadNumAndAdvance(&it, ' ');

  if (!month || !day || !hour || !minute || !sec || !ns || !pid || !tid) {
    context_->storage->IncrementStats(stats::android_log_num_failed);
    return base::OkStatus();
  }

  if (it.size() < 4 || it.at(1) != ' ') {
    context_->storage->IncrementStats(stats::android_log_num_failed);
    return base::OkStatus();
  }

  char prio_str = it.at(0);
  int prio = protos::pbzero::AndroidLogPriority::PRIO_UNSPECIFIED;
  if ('V' == prio_str) {
    prio = protos::pbzero::AndroidLogPriority::PRIO_VERBOSE;
  } else if ('D' == prio_str) {
    prio = protos::pbzero::AndroidLogPriority::PRIO_DEBUG;
  } else if ('I' == prio_str) {
    prio = protos::pbzero::AndroidLogPriority::PRIO_INFO;
  } else if ('W' == prio_str) {
    prio = protos::pbzero::AndroidLogPriority::PRIO_WARN;
  } else if ('E' == prio_str) {
    prio = protos::pbzero::AndroidLogPriority::PRIO_ERROR;
  } else if ('F' == prio_str) {
    prio = protos::pbzero::AndroidLogPriority::PRIO_FATAL;
  }

  it = it.substr(2);

  // Find the ': ' that defines the boundary between the tag and message.
  // We can't just look for ':' because various HALs emit tags with a ':'.
  base::StringView cat;
  for (size_t i = 0; i < it.size() - 1; ++i) {
    if (it.at(i) == ':' && it.at(i + 1) == ' ') {
      cat = it.substr(0, i);
      it = it.substr(i + 2);
      break;
    }
  }
  // Trim trailing spaces, happens in kernel events (e.g. "init   :").
  while (!cat.empty() && cat.at(cat.size() - 1) == ' ')
    cat = cat.substr(0, cat.size() - 1);

  base::StringView msg = it;  // The rest is the log message.

  int64_t secs = base::MkTime(year_, *month, *day, *hour, *minute, *sec);
  std::chrono::nanoseconds event_ts(secs * 1000000000ll + *ns);

  AndroidLogEvent event;
  event.pid = static_cast<uint32_t>(*pid);
  event.tid = static_cast<uint32_t>(*tid);
  event.prio = static_cast<uint32_t>(prio);
  event.tag = context_->storage->InternString(cat);
  event.msg = context_->storage->InternString(msg);

  return ProcessEvent(event_ts, std::move(event));
}

base::Status AndroidLogReader::ProcessEvent(std::chrono::nanoseconds event_ts,
                                            AndroidLogEvent event) {
  if (wait_for_tz_) {
    if (!context_->clock_tracker->timezone_offset().has_value()) {
      non_tz_adjusted_events_.push_back(TimestampedAndroidLogEvent{
          std::chrono::duration_cast<std::chrono::milliseconds>(event_ts),
          std::move(event), false});
      return base::OkStatus();
    } else {
      RETURN_IF_ERROR(FlushNonTzAdjustedEvents());
    }
  }
  return SendToSorter(event_ts, std::move(event));
}

base::Status AndroidLogReader::SendToSorter(std::chrono::nanoseconds event_ts,
                                            AndroidLogEvent event) {
  int64_t ts =
      event_ts.count() - context_->clock_tracker->timezone_offset().value_or(0);
  ASSIGN_OR_RETURN(int64_t trace_ts,
                   context_->clock_tracker->ToTraceTime(
                       protos::pbzero::ClockSnapshot::Clock::REALTIME, ts));
  context_->sorter->PushAndroidLogEvent(trace_ts, std::move(event));
  return base::OkStatus();
}

base::Status AndroidLogReader::FlushNonTzAdjustedEvents() {
  for (const TimestampedAndroidLogEvent& event : non_tz_adjusted_events_) {
    RETURN_IF_ERROR(SendToSorter(event.ts, event.event));
  }
  non_tz_adjusted_events_.clear();
  return base::OkStatus();
}

void AndroidLogReader::EndOfStream(base::StringView) {
  // Flush all events once we reach the end of input, regarddless of if we got
  // a TZ offset or not.
  FlushNonTzAdjustedEvents();
}

BufferingAndroidLogReader::~BufferingAndroidLogReader() = default;

base::Status BufferingAndroidLogReader::ProcessEvent(
    std::chrono::nanoseconds event_ts,
    AndroidLogEvent event) {
  RETURN_IF_ERROR(AndroidLogReader::ProcessEvent(event_ts, event));
  events_.push_back(TimestampedAndroidLogEvent{
      std::chrono::duration_cast<std::chrono::milliseconds>(event_ts),
      std::move(event), false});
  return base::OkStatus();
}

DedupingAndroidLogReader::DedupingAndroidLogReader(
    TraceProcessorContext* context,
    int32_t year,
    bool wait_for_tz,
    std::vector<TimestampedAndroidLogEvent> events)
    : AndroidLogReader(context, year, wait_for_tz), events_(std::move(events)) {
  std::sort(events_.begin(), events_.end());
}

DedupingAndroidLogReader::~DedupingAndroidLogReader() {}

base::Status DedupingAndroidLogReader::ProcessEvent(
    std::chrono::nanoseconds event_ts,
    AndroidLogEvent event) {
  const auto comp = [](const TimestampedAndroidLogEvent& lhs,
                       std::chrono::milliseconds rhs_time) {
    return lhs.ts < rhs_time;
  };

  const auto event_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(event_ts);

  for (auto it =
           std::lower_bound(events_.begin(), events_.end(), event_ms, comp);
       it != events_.end() && it->ts == event_ms; ++it) {
    // Duplicate found
    if (!it->matched && it->event == event) {
      // "Remove" the entry from the list
      it->matched = true;
      return base::OkStatus();
    }
  }

  return AndroidLogReader::ProcessEvent(event_ts, std::move(event));
}

}  // namespace perfetto::trace_processor
