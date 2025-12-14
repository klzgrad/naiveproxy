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

#include "perfetto/base/logging.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

ImportLogsTracker::ImportLogsTracker(TraceProcessorContext* context,
                                     uint32_t trace_id)
    : context_(context),
      trace_id_(trace_id),
      severity_info_id_(context_->storage->InternString("info")),
      severity_data_loss_id_(context_->storage->InternString("data_loss")),
      severity_error_id_(context_->storage->InternString("error")) {}

void ImportLogsTracker::RecordImportLog(
    size_t stat_key,
    std::optional<int64_t> timestamp,
    std::optional<int64_t> byte_offset,
    std::function<void(ArgsTracker::BoundInserter&)> args_callback) {
  PERFETTO_CHECK(stats::kSources[stat_key] == stats::Source::kAnalysis);

  context_->storage->IncrementStats(stat_key);

  tables::TraceImportLogsTable::Row row;
  row.trace_id = trace_id_;
  row.ts = timestamp;
  row.byte_offset = byte_offset;
  row.severity = SeverityToStringId(stats::kSeverities[stat_key]);
  row.name = context_->storage->InternString(
      base::StringView(stats::kNames[stat_key]));

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

StringId ImportLogsTracker::SeverityToStringId(stats::Severity severity) {
  switch (severity) {
    case stats::Severity::kInfo:
      return severity_info_id_;
    case stats::Severity::kDataLoss:
      return severity_data_loss_id_;
    case stats::Severity::kError:
      return severity_error_id_;
  }
  PERFETTO_FATAL("Unknown severity");
}

}  // namespace perfetto::trace_processor
