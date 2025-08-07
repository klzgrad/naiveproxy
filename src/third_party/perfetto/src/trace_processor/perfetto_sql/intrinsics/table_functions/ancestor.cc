/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/ancestor.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/tables_py.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

namespace {

template <typename T>
bool GetAncestors(const T& table,
                  typename T::Id starting_id,
                  std::vector<typename T::RowNumber>& row_numbers_accumulator,
                  base::Status& out_status) {
  auto start_ref = table.FindById(starting_id);
  if (!start_ref) {
    out_status = base::ErrStatus("no row with id %" PRIu32 "",
                                 static_cast<uint32_t>(starting_id.value));
    return false;
  }

  // It's important we insert directly into |row_numbers_accumulator| and not
  // overwrite it because we expect the existing elements in
  // |row_numbers_accumulator| to be preserved.
  auto maybe_parent_id = start_ref->parent_id();
  while (maybe_parent_id) {
    auto ref = *table.FindById(*maybe_parent_id);
    row_numbers_accumulator.emplace_back(ref.ToRowNumber());
    // Update the loop variable by looking up the next parent_id.
    maybe_parent_id = ref.parent_id();
  }
  // We traverse the tree in reverse id order. To ensure we meet the
  // requirements of the extension vectors being sorted, ensure that we reverse
  // the row numbers to be in id order.
  std::reverse(row_numbers_accumulator.begin(), row_numbers_accumulator.end());
  return true;
}

}  // namespace

Ancestor::SliceCursor::SliceCursor(Type type, TraceStorage* storage)
    : type_(type),
      storage_(storage),
      table_(storage->mutable_string_pool()),
      stack_cursor_(storage->slice_table().CreateCursor({dataframe::FilterSpec{
          tables::SliceTable::ColumnIndex::stack_id,
          0,
          dataframe::Eq{},
          std::nullopt,
      }})) {}

bool Ancestor::SliceCursor::Run(const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 1);

  // Clear all our temporary state.
  ancestors_.clear();
  table_.Clear();

  if (arguments[0].is_null()) {
    // Nothing matches a null id so return an empty table.
    return OnSuccess(&table_.dataframe());
  }
  if (arguments[0].type != SqlValue::Type::kLong) {
    return OnFailure(base::ErrStatus("start id should be an integer."));
  }
  const auto& slice_table = storage_->slice_table();
  switch (type_) {
    case Type::kSlice: {
      auto id = static_cast<uint32_t>(arguments[0].long_value);
      if (!GetAncestors(slice_table, SliceId(id), ancestors_, status_)) {
        return false;
      }
      break;
    }
    case Type::kSliceByStack: {
      // Find the all slice ids that have the stack id and find all the
      // ancestors of the slice ids.
      stack_cursor_.SetFilterValueUnchecked(0, arguments[0].long_value);
      stack_cursor_.Execute();
      for (; !stack_cursor_.Eof(); stack_cursor_.Next()) {
        if (!GetAncestors(slice_table, stack_cursor_.id(), ancestors_,
                          status_)) {
          return false;
        }
      }
      // Sort to keep the slices in timestamp order.
      std::sort(ancestors_.begin(), ancestors_.end());
      break;
    }
    case Type::kStackProfileCallsite:
      PERFETTO_FATAL("Unreachable");
  }
  for (const auto& ancestor_row : ancestors_) {
    auto ref = ancestor_row.ToRowReference(slice_table);
    table_.Insert(tables::SliceSubsetTable::Row{
        ref.id(),
        ref.ts(),
        ref.dur(),
        ref.track_id(),
        ref.category(),
        ref.name(),
        ref.depth(),
        ref.parent_id(),
        ref.arg_set_id(),
        ref.thread_ts(),
        ref.thread_dur(),
        ref.thread_instruction_count(),
        ref.thread_instruction_delta(),
    });
  }
  return OnSuccess(&table_.dataframe());
}

Ancestor::StackProfileCursor::StackProfileCursor(TraceStorage* storage)
    : storage_(storage), table_(storage->mutable_string_pool()) {}

bool Ancestor::StackProfileCursor::Run(const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 1);

  // Clear all our temporary state.
  ancestors_.clear();
  table_.Clear();

  if (arguments[0].is_null()) {
    // Nothing matches a null id so return an empty table.
    return OnSuccess(&table_.dataframe());
  }
  if (arguments[0].type != SqlValue::Type::kLong) {
    return OnFailure(base::ErrStatus("start id should be an integer."));
  }
  const auto& callsite = storage_->stack_profile_callsite_table();
  auto id = static_cast<uint32_t>(arguments[0].long_value);
  if (!GetAncestors(callsite, CallsiteId(id), ancestors_, status_)) {
    return false;
  }
  for (const auto& ancestor_row : ancestors_) {
    auto ref = ancestor_row.ToRowReference(callsite);
    table_.Insert(tables::AncestorStackProfileCallsiteTable::Row{
        ref.id(),
        ref.depth(),
        ref.parent_id(),
        ref.frame_id(),
    });
  }
  return OnSuccess(&table_.dataframe());
}

Ancestor::Ancestor(Type type, TraceStorage* storage)
    : type_(type), storage_(storage) {}

std::unique_ptr<StaticTableFunction::Cursor> Ancestor::MakeCursor() {
  switch (type_) {
    case Type::kSlice:
    case Type::kSliceByStack:
      return std::make_unique<SliceCursor>(type_, storage_);
    case Type::kStackProfileCallsite:
      return std::make_unique<StackProfileCursor>(storage_);
  }
  PERFETTO_FATAL("For GCC");
}

dataframe::DataframeSpec Ancestor::CreateSpec() {
  switch (type_) {
    case Type::kSlice:
    case Type::kSliceByStack:
      return tables::SliceSubsetTable::kSpec.ToUntypedDataframeSpec();
    case Type::kStackProfileCallsite:
      return tables::AncestorStackProfileCallsiteTable::kSpec
          .ToUntypedDataframeSpec();
  }
  PERFETTO_FATAL("For GCC");
}

std::string Ancestor::TableName() {
  switch (type_) {
    case Type::kSlice:
      return "ancestor_slice";
    case Type::kSliceByStack:
      return "ancestor_slice_by_stack";
    case Type::kStackProfileCallsite:
      return "experimental_ancestor_stack_profile_callsite";
  }
  PERFETTO_FATAL("For GCC");
}

uint32_t Ancestor::GetArgumentCount() const {
  return 1;
}
uint32_t Ancestor::EstimateRowCount() {
  return 1;
}

// static
bool Ancestor::GetAncestorSlices(
    const tables::SliceTable& slices,
    SliceId slice_id,
    std::vector<tables::SliceTable::RowNumber>& ret,
    base::Status& out_status) {
  return GetAncestors(slices, slice_id, ret, out_status);
}

}  // namespace perfetto::trace_processor
