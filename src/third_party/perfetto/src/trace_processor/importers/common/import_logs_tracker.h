/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_IMPORT_LOGS_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_IMPORT_LOGS_TRACKER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Tracks import-time errors and warnings, recording them both as stats
// (for aggregate metrics) and in the TraceImportLogsTable (for detailed,
// queryable logs with context).
class ImportLogsTracker {
 public:
  explicit ImportLogsTracker(TraceProcessorContext*, uint32_t trace_id);

  // For "tokenization" errors (pre-parsing, only have byte offset)
  // Use when reading raw bytes and encountering malformed data.
  void RecordTokenizationError(
      size_t stat_key,
      int64_t byte_offset,
      std::function<void(ArgsTracker::BoundInserter&)> args_callback = {});

  // For "parser" errors (post-parsing, have timestamp + context)
  // Use when you have a parsed event but it's invalid/problematic.
  void RecordParserError(
      size_t stat_key,
      int64_t timestamp,
      std::function<void(ArgsTracker::BoundInserter&)> args_callback = {});

 private:
  void RecordImportLog(
      size_t stat_key,
      std::optional<int64_t> timestamp,
      std::optional<int64_t> byte_offset,
      std::function<void(ArgsTracker::BoundInserter&)> args_callback);

  TraceProcessorContext* context_;
  uint32_t trace_id_;

  // Cached string IDs for severity levels
  StringId severity_info_id_;
  StringId severity_data_loss_id_;
  StringId severity_error_id_;

  StringId SeverityToStringId(stats::Severity severity);
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_IMPORT_LOGS_TRACKER_H_
