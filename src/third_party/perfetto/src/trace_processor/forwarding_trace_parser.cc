/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/forwarding_trace_parser.h"

#include <memory>
#include <optional>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/importers/proto/proto_trace_reader.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/trace_reader_registry.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor {
namespace {

TraceSorter::SortingMode ConvertSortingMode(SortingMode sorting_mode) {
  switch (sorting_mode) {
    case SortingMode::kDefaultHeuristics:
      return TraceSorter::SortingMode::kDefault;
    case SortingMode::kForceFullSort:
      return TraceSorter::SortingMode::kFullSort;
  }
  PERFETTO_FATAL("For GCC");
}

std::optional<TraceSorter::SortingMode> GetMinimumSortingMode(
    TraceType trace_type,
    const TraceProcessorContext& context) {
  switch (trace_type) {
    case kGzipTraceType:
      return std::nullopt;

    case kAndroidDumpstateTraceType:
    case kAndroidLogcatTraceType:
    case kArtHprofTraceType:
    case kArtMethodTraceType:
    case kCtraceTraceType:
    case kFuchsiaTraceType:
    case kGeckoTraceType:
    case kInstrumentsXmlTraceType:
    case kJsonTraceType:
    case kNinjaLogTraceType:
    case kPerfDataTraceType:
    case kPerfTextTraceType:
    case kPprofTraceType:
    case kSimpleperfProtoTraceType:
    case kSystraceTraceType:
    case kTarTraceType:
    case kUnknownTraceType:
    case kZipFile:
      return TraceSorter::SortingMode::kFullSort;

    case kProtoTraceType:
    case kSymbolsTraceType:
      return ConvertSortingMode(context.config.sorting_mode);

    case kAndroidBugreportTraceType:
      PERFETTO_FATAL(
          "This trace type should be handled at the ZipParser level");
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace

ForwardingTraceParser::ForwardingTraceParser(TraceProcessorContext* context,
                                             tables::TraceFileTable::Id id)
    : input_context_(context), file_id_(id) {}

ForwardingTraceParser::~ForwardingTraceParser() = default;

base::Status ForwardingTraceParser::Init(const TraceBlobView& blob) {
  PERFETTO_CHECK(!reader_);

  {
    auto scoped_trace = input_context_->storage->TraceExecutionTimeIntoStats(
        stats::guess_trace_type_duration_ns);
    trace_type_ = GuessTraceType(blob.data(), blob.size());
  }
  if (trace_type_ == kUnknownTraceType) {
    // If renaming this error message don't remove the "(ERR:fmt)" part.
    // The UI's error_dialog.ts uses it to make the dialog more graceful.
    return base::ErrStatus("Unknown trace type provided (ERR:fmt)");
  }
  PERFETTO_DLOG("%s trace detected", TraceTypeToString(trace_type_));

  if (file_id_.value != 0 && trace_type_ == kNinjaLogTraceType) {
    return base::ErrStatus(
        "Ninja traces currently do not support being contained inside other "
        "trace formats. Please file a bug at "
        "https://github.com/google/perfetto/issues if this is important to "
        "you.");
  }

  std::optional<TraceSorter::SortingMode> minimum_sorting_mode =
      GetMinimumSortingMode(trace_type_, *input_context_);
  if (minimum_sorting_mode) {
    input_context_->sorter->SetSortingMode(*minimum_sorting_mode);
  }
  input_context_->trace_file_tracker->StartParsing(file_id_, trace_type_);

  // TODO(b/334978369) Make sure kProtoTraceType and kSystraceTraceType are
  // parsed first so that we do not get issues with
  // SetPidZeroIsUpidZeroIdleProcess()
  trace_context_ = input_context_->ForkContextForTrace(file_id_.value, 0);
  if (trace_type_ == kProtoTraceType || trace_type_ == kSystraceTraceType) {
    trace_context_->process_tracker->SetPidZeroIsUpidZeroIdleProcess();
  }
  ASSIGN_OR_RETURN(reader_, input_context_->reader_registry->CreateTraceReader(
                                trace_type_, trace_context_));
  return base::OkStatus();
}

base::Status ForwardingTraceParser::Parse(TraceBlobView blob) {
  // If this is the first Parse() call, guess the trace type and create the
  // appropriate parser.
  if (!reader_) {
    RETURN_IF_ERROR(Init(blob));
  }
  trace_size_ += blob.size();
  return reader_->Parse(std::move(blob));
}

base::Status ForwardingTraceParser::NotifyEndOfFile() {
  if (reader_) {
    RETURN_IF_ERROR(reader_->NotifyEndOfFile());
  }
  if (trace_type_ != kUnknownTraceType) {
    input_context_->trace_file_tracker->DoneParsing(file_id_, trace_size_);
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
