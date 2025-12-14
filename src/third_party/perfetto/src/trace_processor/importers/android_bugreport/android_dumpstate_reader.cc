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

#include "src/trace_processor/importers/android_bugreport/android_dumpstate_reader.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/string_view_splitter.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "src/trace_processor/importers/android_bugreport/android_battery_stats_reader.h"
#include "src/trace_processor/importers/android_bugreport/android_log_reader.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

AndroidDumpstateReader::AndroidDumpstateReader(TraceProcessorContext* context)
    : context_(context), battery_stats_reader_(context) {}

AndroidDumpstateReader::~AndroidDumpstateReader() = default;

base::Status AndroidDumpstateReader::ParseLine(base::StringView line) {
  if (!default_log_reader_) {
    default_log_reader_ = std::make_unique<BufferingAndroidLogReader>(
        context_, /* year */ 0, /*wait_for_tz*/ true);
  }
  return ParseLine(default_log_reader_.get(), line);
}

base::Status AndroidDumpstateReader::ParseLine(
    BufferingAndroidLogReader* const log_reader,
    base::StringView line) {
  context_->clock_tracker->SetTraceTimeClock(
      protos::pbzero::BUILTIN_CLOCK_REALTIME);

  // Dumpstate is organized in a two level hierarchy, beautifully flattened into
  // one text file with load bearing ----- markers:
  // 1. Various dumpstate sections, examples:
  // ```
  //   ------ DUMPSYS CRITICAL (/system/bin/dumpsys) ------
  //   ...
  //   ------ SYSTEM LOG (logcat -v threadtime -v printable -v uid) ------
  //   ...
  //   ------ IPTABLES (iptables -L -nvx) ------
  //   ...
  //   ------ DUMPSYS HIGH (/system/bin/dumpsys) ------
  //   ...
  //   ------ DUMPSYS (/system/bin/dumpsys) ------
  // ```
  //
  // 2. Within the "------ DUMPSYS" section (note dumpsys != dumpstate), there
  //    are multiple services. Note that there are at least 3 DUMPSYS sections
  //    (CRITICAL, HIGH and default), with multiple services in each:
  // ```
  //    ------ DUMPSYS (/system/bin/dumpsys) ------
  // DUMP OF SERVICE activity:
  // ...
  // ---------------------------------------------------------------------------
  // DUMP OF SERVICE input_method:
  // ...
  // ---------------------------------------------------------------------------
  // ```
  // Here we put each line in a dedicated table, android_dumpstate, keeping
  // track of the dumpstate `section` and dumpsys `service`.
  static constexpr size_t npos = base::StringView::npos;
  if (line.StartsWith("------ ") && line.EndsWith(" ------")) {
    // These lines mark the beginning and end of dumpstate sections:
    // ------ DUMPSYS CRITICAL (/system/bin/dumpsys) ------
    // ------ 0.356s was the duration of 'DUMPSYS CRITICAL' ------
    base::StringView section = line.substr(7);
    section = section.substr(0, section.size() - 7);
    bool end_marker = section.find("was the duration of") != npos;
    current_service_id_ = StringId::Null();
    current_service_ = "";
    if (end_marker) {
      current_section_id_ = StringId::Null();
    } else {
      current_section_id_ = context_->storage->InternString(section);
      current_section_ = Section::kOther;
      if (section.StartsWith("DUMPSYS")) {
        current_section_ = Section::kDumpsys;
      } else if (section.StartsWith("SYSTEM LOG") ||
                 section.StartsWith("EVENT LOG") ||
                 section.StartsWith("RADIO LOG")) {
        // KERNEL LOG is deliberately omitted because SYSTEM LOG is a
        // superset. KERNEL LOG contains all dupes.
        current_section_ = Section::kLog;
      } else if (section.StartsWith("BLOCK STAT")) {
        // Coalesce all the block stats into one section. Otherwise they
        // pollute the table with one section per block device.
        current_section_id_ = context_->storage->InternString("BLOCK STAT");
      } else if (section.StartsWith("CHECKIN BATTERYSTATS")) {
        current_section_ = Section::kBatteryStats;
      }
    }
    return base::OkStatus();
  }
  // Skip end marker lines for dumpsys sections.
  if (current_section_ == Section::kDumpsys && line.StartsWith("--------- ") &&
      line.find("was the duration of dumpsys") != npos) {
    current_service_id_ = StringId::Null();
    current_service_ = "";
    return base::OkStatus();
  }
  if (current_section_ == Section::kDumpsys && current_service_id_.is_null() &&
      line.StartsWith("----------------------------------------------")) {
    return base::OkStatus();
  }
  // if we get the start of a standalone battery stats checkin, then set the
  // section and deliberately fall though so we we can parse the line.
  if (line.StartsWith("9,0,i,vers,")) {
    current_section_ = Section::kBatteryStats;
  }
  if (current_section_ == Section::kDumpsys &&
      line.StartsWith("DUMP OF SERVICE")) {
    // DUMP OF SERVICE [CRITICAL|HIGH] ServiceName:
    base::StringView svc = line.substr(line.rfind(' ') + 1);
    svc = svc.substr(0, svc.size() - 1);
    current_service_id_ = context_->storage->InternString(svc);
    current_service_ = svc;
  } else if (current_section_ == Section::kDumpsys &&
             current_service_ == "alarm") {
    MaybeSetTzOffsetFromAlarmService(line);
  } else if (current_section_ == Section::kLog) {
    PERFETTO_DCHECK(log_reader != nullptr);
    RETURN_IF_ERROR(log_reader->ParseLine(line));
  } else if (current_section_ == Section::kBatteryStats) {
    RETURN_IF_ERROR(battery_stats_reader_.ParseLine(line));
  }

  // Append the line to the android_dumpstate table.
  context_->storage->mutable_android_dumpstate_table()->Insert(
      {current_section_id_, current_service_id_,
       context_->storage->InternString(line)});

  return base::OkStatus();
}

void AndroidDumpstateReader::MaybeSetTzOffsetFromAlarmService(
    base::StringView line) {
  // attempt to parse the line if it has the following form:
  //  nowRTC=1629844744041=2021-08-24 23:39:04.041 nowELAPSED=403532
  if (line.StartsWith("  nowRTC=")) {
    size_t end_of_rtc_str = line.find(" nowELAPSED=");
    if (end_of_rtc_str == base::StringView::npos) {
      return;
    }
    base::StringViewSplitter svs(line.substr(0, end_of_rtc_str), '=');

    // discard the first token which is just "nowRTC"
    svs.NextToken();

    // Parse the UTC integer timestamp
    std::optional<int64_t> non_tz_adjusted_ts_ms =
        base::StringViewToInt64(svs.NextToken());
    if (!non_tz_adjusted_ts_ms.has_value()) {
      return;
    }

    // Parse tz adjusted string in the form "2021-08-24 23:38:14.510".
    // The milliseconds part will be handled separately since base::MkTime()
    // only supports seconds precision.
    base::StringViewSplitter date_decimal_splitter(svs.NextToken(), '.');

    // Extract the date and time string (e.g., "2021-08-24 23:38:14").
    base::StringView date_and_time_str = date_decimal_splitter.NextToken();

    // Extract the milliseconds part (e.g., "510").
    base::StringView ms_part_str = date_decimal_splitter.NextToken();

    // Split the date and time (eg. "2021-08-24") into separate components.
    base::StringViewSplitter date_and_time_splitter(date_and_time_str, ' ');
    base::StringView date_str = date_and_time_splitter.NextToken();
    base::StringView time_str = date_and_time_splitter.NextToken();

    // Split the date into year, month, and day.
    base::StringViewSplitter date_splitter(date_str, '-');
    std::optional<int> year =
        base::StringViewToInt32(date_splitter.NextToken());
    std::optional<int> month =
        base::StringViewToInt32(date_splitter.NextToken());
    std::optional<int> day = base::StringViewToInt32(date_splitter.NextToken());
    if (!year.has_value() || !month.has_value() || !day.has_value()) {
      return;
    }

    // Split the time into hour, minute, and second.
    base::StringViewSplitter time_splitter(time_str, ':');
    std::optional<int> hour =
        base::StringViewToInt32(time_splitter.NextToken());
    std::optional<int> minute =
        base::StringViewToInt32(time_splitter.NextToken());
    std::optional<int> second =
        base::StringViewToInt32(time_splitter.NextToken());
    if (!hour.has_value() || !minute.has_value() || !second.has_value()) {
      return;
    }

    // Compute the timestamp in milliseconds (seconds granularity).
    int64_t tz_adjusted_ts_ms =
        base::MkTime(year.value(), month.value(), day.value(), hour.value(),
                     minute.value(), second.value()) *
        1000L;

    // Parse and add the milliseconds component.
    std::optional<int64_t> ms_part = base::StringViewToInt64(ms_part_str);
    if (!ms_part.has_value()) {
      return;
    }
    tz_adjusted_ts_ms += ms_part.value();

    // Compute the timezone offset in nanoseconds and set it.
    int64_t tz_offset_ns =
        (tz_adjusted_ts_ms - non_tz_adjusted_ts_ms.value()) * 1000 * 1000;
    context_->clock_tracker->set_timezone_offset(tz_offset_ns);
  }
}

void AndroidDumpstateReader::EndOfStream(base::StringView) {}

}  // namespace perfetto::trace_processor
