/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/shell/shell_utils.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/trace_processor.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <unistd.h>
#endif

namespace perfetto::trace_processor {
namespace {

class FileExportOutput : public TraceProcessor::ExportOutput {
 public:
  explicit FileExportOutput(std::string path) : path_(std::move(path)) {}

  base::Status Write(const void* data, size_t size) override {
    if (!file_) {
      file_ = base::OpenFile(path_, O_CREAT | O_WRONLY | O_TRUNC, 0644);
      if (!file_) {
        return base::ErrStatus("Failed to create file: %s", path_.c_str());
      }
    }
    if (base::WriteAll(file_.get(), data, size) != static_cast<ssize_t>(size)) {
      return base::ErrStatus("Failed to write export output");
    }
    return base::OkStatus();
  }

  std::optional<std::string> GetFilePath() const override { return path_; }

 private:
  std::string path_;
  base::ScopedFile file_;
};

}  // namespace

bool StderrSupportsColors() {
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM) && \
    !PERFETTO_BUILDFLAG(PERFETTO_CHROMIUM_BUILD)
  static const bool use_colors = isatty(STDERR_FILENO);
  return use_colors;
#else
  return false;
#endif
}

namespace {

base::Status PrintStatsSection(TraceProcessor* tp,
                               const char* header,
                               const char* color,
                               const char* query) {
  const bool colors = StderrSupportsColors();
  auto it = tp->ExecuteQuery(query);
  bool first = true;
  while (it.Next()) {
    if (first) {
      base::StackString<256> line("  %s%s%s", colors ? color : "", header,
                                  colors ? "\x1b[0m" : "");
      fprintf(stderr, "%s\n", line.c_str());
      first = false;
    }
    // Columns: name, idx, value, description.
    const char* name = it.Get(0).string_value;
    SqlValue idx = it.Get(1);
    int64_t value = it.Get(2).long_value;
    const char* description = it.Get(3).string_value;

    if (idx.type == SqlValue::Type::kNull) {
      base::StackString<512> line("    %s: %" PRIi64, name, value);
      fprintf(stderr, "%s", line.c_str());
    } else {
      base::StackString<512> line("    %s[%" PRIi64 "]: %" PRIi64, name,
                                  idx.long_value, value);
      fprintf(stderr, "%s", line.c_str());
    }
    if (description && description[0] != '\0') {
      base::StackString<512> desc(" | %s", description);
      fprintf(stderr, "%s", desc.c_str());
    }
    fprintf(stderr, "\n");
  }
  if (!first) {
    fprintf(stderr, "\n");
  }
  base::Status status = it.Status();
  if (!status.ok()) {
    return base::ErrStatus("Error while iterating stats (%s)",
                           status.c_message());
  }
  return base::OkStatus();
}

}  // namespace

base::Status PrintStats(TraceProcessor* tp) {
  // Quick check: are there any issues at all?
  auto check = tp->ExecuteQuery(
      "SELECT 1 FROM stats "
      "WHERE severity IN ('error', 'data_loss') AND value > 0 LIMIT 1");
  bool has_issues = check.Next();
  {
    base::Status s = check.Status();
    if (!s.ok())
      return s;
  }
  if (!has_issues)
    return base::OkStatus();

  const bool colors = StderrSupportsColors();
  base::StackString<64> title("\n%sTrace health issues:%s\n",
                              colors ? "\x1b[1;33m" : "",
                              colors ? "\x1b[0m" : "");
  fprintf(stderr, "\n%s\n", title.c_str());

  RETURN_IF_ERROR(PrintStatsSection(
      tp, "Trace errors", "\x1b[1;31m",
      "SELECT name, idx, value, description FROM stats "
      "WHERE severity = 'error' AND source = 'trace' AND value > 0 "
      "ORDER BY name, idx"));

  RETURN_IF_ERROR(PrintStatsSection(
      tp, "Import errors", "\x1b[1;31m",
      "SELECT name, idx, value, description FROM stats "
      "WHERE severity = 'error' AND source = 'analysis' AND value > 0 "
      "ORDER BY name, idx"));

  RETURN_IF_ERROR(
      PrintStatsSection(tp, "Data losses", "\x1b[1;33m",
                        "SELECT name, idx, value, description FROM stats "
                        "WHERE severity = 'data_loss' AND value > 0 "
                        "ORDER BY name, idx"));

  return base::OkStatus();
}

base::Status ExportTraceToDatabase(TraceProcessor* trace_processor,
                                   const std::string& output_name) {
  return ExportTrace(trace_processor, TraceProcessor::ExportFormat::kSqlite,
                     output_name);
}

base::Status ExportTrace(TraceProcessor* trace_processor,
                         TraceProcessor::ExportFormat format,
                         const std::string& output_name) {
  FileExportOutput output(output_name);
  return trace_processor->Export(format, &output);
}

}  // namespace perfetto::trace_processor
