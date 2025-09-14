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

#include "src/trace_processor/importers/common/trace_file_tracker.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor {

tables::TraceFileTable::Id TraceFileTracker::AddFile(const std::string& name) {
  return AddFileImpl(context_->storage->InternString(base::StringView(name)));
}

tables::TraceFileTable::Id TraceFileTracker::AddFileImpl(StringId name) {
  std::optional<tables::TraceFileTable::Id> parent =
      parsing_stack_.empty() ? std::nullopt
                             : std::make_optional(parsing_stack_.back());
  return context_->storage->mutable_trace_file_table()
      ->Insert({parent, name, 0,
                context_->storage->InternString(
                    TraceTypeToString(kUnknownTraceType)),
                std::nullopt})
      .id;
}

void TraceFileTracker::SetSize(tables::TraceFileTable::Id id, uint64_t size) {
  auto row = *context_->storage->mutable_trace_file_table()->FindById(id);
  row.set_size(static_cast<int64_t>(size));
}

void TraceFileTracker::StartParsing(tables::TraceFileTable::Id id,
                                    TraceType trace_type) {
  parsing_stack_.push_back(id);
  auto row = *context_->storage->mutable_trace_file_table()->FindById(id);
  row.set_trace_type(
      context_->storage->InternString(TraceTypeToString(trace_type)));
  row.set_processing_order(static_cast<int64_t>(processing_order_++));
}

void TraceFileTracker::DoneParsing(tables::TraceFileTable::Id id, size_t size) {
  PERFETTO_CHECK(!parsing_stack_.empty() && parsing_stack_.back() == id);
  parsing_stack_.pop_back();
  auto row = *context_->storage->mutable_trace_file_table()->FindById(id);
  row.set_size(static_cast<int64_t>(size));

  // First file (root)
  if (id.value == 0) {
    context_->metadata_tracker->SetMetadata(metadata::trace_size_bytes,
                                            Variadic::Integer(row.size()));
    context_->metadata_tracker->SetMetadata(metadata::trace_type,
                                            Variadic::String(row.trace_type()));
  }
}

}  // namespace perfetto::trace_processor
