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

#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

ImportLogsTracker::ImportLogsTracker(TraceProcessorContext* context,
                                     tables::TraceFileTable::Id trace_id)
    : context_(context), trace_id_(trace_id) {}

void ImportLogsTracker::RecordImportLog(
    size_t stat_key,
    std::optional<int64_t> timestamp,
    std::optional<int64_t> byte_offset,
    std::function<void(ArgsTracker::BoundInserter&)> args_callback) {
  context_->storage->IncrementStats(stat_key);

  tables::TraceImportLogsTable::Row row;
  row.trace_id = trace_id_;
  row.ts = timestamp;
  row.byte_offset = byte_offset;
  row.stat_key = static_cast<int64_t>(stat_key);

  auto id =
      context_->storage->mutable_trace_import_logs_table()->Insert(row).id;

  if (args_callback) {
    ArgsTracker args_tracker(context_);
    auto bound_inserter = args_tracker.AddArgsTo(id);
    args_callback(bound_inserter);
  }
}

void ImportLogsTracker::RecordTokenizationError(
    size_t stat_key,
    int64_t byte_offset,
    std::function<void(ArgsTracker::BoundInserter&)> args_callback) {
  RecordImportLog(stat_key,
                  /*timestamp=*/std::nullopt, byte_offset,
                  std::move(args_callback));
}

void ImportLogsTracker::RecordParserError(
    size_t stat_key,
    int64_t timestamp,
    std::function<void(ArgsTracker::BoundInserter&)> args_callback) {
  RecordImportLog(stat_key, timestamp,
                  /*byte_offset=*/std::nullopt, std::move(args_callback));
}

void ImportLogsTracker::RecordAnalysisError(
    size_t stat_key,
    std::function<void(ArgsTracker::BoundInserter&)> args_callback) {
  RecordImportLog(stat_key,
                  /*timestamp=*/std::nullopt,
                  /*byte_offset=*/std::nullopt, std::move(args_callback));
}

}  // namespace perfetto::trace_processor
