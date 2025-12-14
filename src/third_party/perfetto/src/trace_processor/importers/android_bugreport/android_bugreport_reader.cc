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

#include "src/trace_processor/importers/android_bugreport/android_bugreport_reader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "src/trace_processor/importers/android_bugreport/android_dumpstate_reader.h"
#include "src/trace_processor/importers/android_bugreport/android_log_reader.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_type.h"
#include "src/trace_processor/util/zip_reader.h"

namespace perfetto::trace_processor {
namespace {

using ZipFileVector = std::vector<util::ZipFile>;

bool IsBugReportFile(const util::ZipFile& zip) {
  return base::StartsWith(zip.name(), "bugreport-") &&
         base::EndsWith(zip.name(), ".txt");
}

bool IsLogFile(const util::ZipFile& file) {
  return base::StartsWith(file.name(), "FS/data/misc/logd/logcat") &&
         !base::EndsWith(file.name(), "logcat.id");
}

// Extracts the year field from the bugreport-xxx.txt file name.
// This is because logcat events have only the month and day.
// This is obviously bugged for cases of bugreports collected across new year
// but we'll live with that.
std::optional<int32_t> ExtractYearFromBugReportFilename(
    const std::string& filename) {
  // Typical name: "bugreport-product-TP1A.220623.001-2022-06-24-16-24-37.txt".
  auto year_str =
      filename.substr(filename.size() - strlen("2022-12-31-23-59-00.txt"), 4);
  return base::StringToInt32(year_str);
}

struct FindBugReportFileResult {
  size_t file_index;
  int32_t year;
};

std::optional<FindBugReportFileResult> FindBugReportFile(
    const ZipFileVector& files) {
  for (size_t i = 0; i < files.size(); ++i) {
    if (!IsBugReportFile(files[i])) {
      continue;
    }
    std::optional<int32_t> year =
        ExtractYearFromBugReportFilename(files[i].name());
    if (!year.has_value()) {
      continue;
    }

    return FindBugReportFileResult{i, *year};
  }

  return std::nullopt;
}

}  // namespace

AndroidBugreportReader::AndroidBugreportReader(TraceProcessorContext* context)
    : context_(context),
      dumpstate_reader_(std::make_unique<AndroidDumpstateReader>(context_)) {}

AndroidBugreportReader::~AndroidBugreportReader() = default;

bool AndroidBugreportReader::IsAndroidBugReport(
    const std::vector<util::ZipFile>& files) {
  return FindBugReportFile(files).has_value();
}

base::Status AndroidBugreportReader::Parse(std::vector<util::ZipFile> files) {
  auto res = FindBugReportFile(files);
  if (!res.has_value()) {
    return base::ErrStatus("Not a bug report");
  }

  // Move the file to the end move it out of the list and pop the back.
  std::swap(files[res->file_index], files.back());
  auto id = context_->trace_file_tracker->AddFile(files.back().name());
  BugReportFile bug_report{id, res->year, std::move(files.back())};
  files.pop_back();

  std::set<LogFile> ordered_log_files;
  for (size_t i = 0; i < files.size(); ++i) {
    id = context_->trace_file_tracker->AddFile(files[i].name());
    // Set size in case we end up not parsing this file.
    context_->trace_file_tracker->SetSize(id, files[i].compressed_size());
    if (!IsLogFile(files[i])) {
      continue;
    }

    int64_t timestamp = files[i].GetDatetime();
    ordered_log_files.insert(LogFile{id, timestamp, std::move(files[i])});
  }

  // All logs in Android bugreports use wall time (which creates problems
  // in case of early boot events before NTP kicks in, which get emitted as
  // 1970), but that is the state of affairs.
  context_->clock_tracker->SetTraceTimeClock(
      protos::pbzero::BUILTIN_CLOCK_REALTIME);

  ASSIGN_OR_RETURN(std::vector<TimestampedAndroidLogEvent> logcat_events,
                   ParseDumpstateTxt(bug_report));
  return ParsePersistentLogcat(bug_report, ordered_log_files,
                               std::move(logcat_events));
}

base::StatusOr<std::vector<TimestampedAndroidLogEvent>>
AndroidBugreportReader::ParseDumpstateTxt(const BugReportFile& bug_report) {
  BufferingAndroidLogReader log_reader(context_, bug_report.year, true);
  context_->trace_file_tracker->StartParsing(bug_report.id,
                                             kAndroidDumpstateTraceType);
  base::Status status = bug_report.file.DecompressLines(
      [&](const std::vector<base::StringView>& lines) {
        for (const base::StringView& line : lines) {
          dumpstate_reader_->ParseLine(&log_reader, line);
        }
      });

  std::vector<TimestampedAndroidLogEvent> logcat_events =
      std::move(log_reader).ConsumeBufferedEvents();
  context_->trace_file_tracker->DoneParsing(
      bug_report.id, bug_report.file.uncompressed_size());
  RETURN_IF_ERROR(status);
  return logcat_events;
}

base::Status AndroidBugreportReader::ParsePersistentLogcat(
    const BugReportFile& bug_report,
    const std::set<LogFile>& ordered_log_files,
    std::vector<TimestampedAndroidLogEvent> logcat_events) {
  DedupingAndroidLogReader log_reader(context_, bug_report.year,
                                      std::move(logcat_events));

  // Push all events into the AndroidLogParser. It will take care of string
  // interning into the pool. Appends entries into `log_events`.
  for (const auto& log_file : ordered_log_files) {
    context_->trace_file_tracker->StartParsing(log_file.id,
                                               kAndroidLogcatTraceType);
    RETURN_IF_ERROR(log_file.file.DecompressLines(
        [&](const std::vector<base::StringView>& lines) {
          for (const auto& line : lines) {
            log_reader.ParseLine(line);
          }
        }));
    context_->trace_file_tracker->DoneParsing(
        log_file.id, log_file.file.uncompressed_size());
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
