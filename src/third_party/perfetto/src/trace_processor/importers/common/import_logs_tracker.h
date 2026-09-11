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
#include <utility>

#include "src/trace_processor/importers/common/args_tracker.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Tracks errors and other notable import-time events, recording them both as
// stats (for aggregate metrics) and in the TraceImportLogsTable (for detailed,
// queryable logs with context).
class ImportLogsTracker {
 public:
  explicit ImportLogsTracker(TraceProcessorContext*,
                             tables::TraceFileTable::Id trace_id);

  // For "tokenization" logs (pre-parsing, only have byte offset).
  // Use when reading raw bytes and encountering malformed data.
  void RecordTokenizationLog(
      size_t stat_key,
      int64_t byte_offset,
      std::function<void(ArgsTracker::BoundInserter&)> args_callback = {});

  // Overload for size_t byte offset (e.g., from TraceBlobView::offset()).
  void RecordTokenizationLog(
      size_t stat_key,
      size_t byte_offset,
      std::function<void(ArgsTracker::BoundInserter&)> args_callback = {}) {
    RecordTokenizationLog(stat_key, static_cast<int64_t>(byte_offset),
                          std::move(args_callback));
  }

  // For "parser" logs (post-parsing, have timestamp + context).
  // Use when you have a parsed event but it's invalid/problematic.
  void RecordParserLog(
      size_t stat_key,
      int64_t timestamp,
      std::function<void(ArgsTracker::BoundInserter&)> args_callback = {});

  // For "collection" logs (e.g. errors occurring during trace recording on
  // device).
  // Use when recording information that was explicitly supplied by the
  // producer.
  void RecordCollectionLog(
      size_t stat_key,
      int64_t timestamp,
      std::function<void(ArgsTracker::BoundInserter&)> args_callback = {});

  // Overload for collection logs not tied to a specific timestamp (e.g.
  // trace-setup errors reported in a summary packet).
  void RecordCollectionLog(
      size_t stat_key,
      std::function<void(ArgsTracker::BoundInserter&)> args_callback);

  // For "analysis" logs (validation/resolution phase, no specific event)
  // Use ONLY when the observation occurs during analysis/validation, not tied
  // to a specific packet or event (e.g., track hierarchy validation).
  // IMPORTANT: This should be rare - prefer RecordTokenizationLog or
  // RecordParserLog when you have context (byte offset or timestamp).
  // IMPORTANT: Since this API has neither timestamp nor byte offset, you MUST
  // provide args_callback with sufficient context to identify and disambiguate
  // the specific error occurrence (e.g., track_uuid, utid, upid, etc.).
  void RecordAnalysisLog(
      size_t stat_key,
      std::function<void(ArgsTracker::BoundInserter&)> args_callback);

 private:
  void RecordImportLog(
      size_t stat_key,
      std::optional<int64_t> timestamp,
      std::optional<int64_t> byte_offset,
      std::function<void(ArgsTracker::BoundInserter&)> args_callback);

  TraceProcessorContext* context_;
  tables::TraceFileTable::Id trace_id_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_IMPORT_LOGS_TRACKER_H_
