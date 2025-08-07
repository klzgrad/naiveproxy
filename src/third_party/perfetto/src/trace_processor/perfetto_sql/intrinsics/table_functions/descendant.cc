/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/descendant.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <limits>
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

bool GetDescendantsInternal(
    const tables::SliceTable& slices,
    tables::SliceTable::ConstCursor& cursor,
    SliceId starting_id,
    std::vector<tables::SliceTable::RowNumber>& row_numbers_accumulator,
    base::Status& out_status) {
  auto start_ref = slices.FindById(starting_id);
  if (!start_ref) {
    out_status =
        base::ErrStatus("no row with id %" PRIu32 "", starting_id.value);
    return false;
  }
  cursor.SetFilterValueUnchecked(0, start_ref->ts());
  cursor.SetFilterValueUnchecked(1, start_ref->track_id().value);
  cursor.SetFilterValueUnchecked(2, start_ref->depth());
  cursor.SetFilterValueUnchecked(3, start_ref->dur() >= 0
                                        ? start_ref->ts() + start_ref->dur()
                                        : std::numeric_limits<int64_t>::max());
  for (cursor.Execute(); !cursor.Eof(); cursor.Next()) {
    row_numbers_accumulator.emplace_back(cursor.ToRowNumber());
  }
  return true;
}

}  // namespace

Descendant::Cursor::Cursor(Type type, TraceStorage* storage)
    : type_(type),
      storage_(storage),
      table_(storage->mutable_string_pool()),
      slice_cursor_(MakeCursor(storage->slice_table())),
      stack_cursor_(storage->slice_table().CreateCursor({
          dataframe::FilterSpec{
              tables::SliceTable::ColumnIndex::stack_id,
              0,
              dataframe::Eq{},
              std::nullopt,
          },
      })) {}

bool Descendant::Cursor::Run(const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 1);

  table_.Clear();
  descendants_.clear();
  if (arguments[0].is_null()) {
    return OnSuccess(&table_.dataframe());
  }
  if (arguments[0].type != SqlValue::Type::kLong) {
    return OnFailure(base::ErrStatus("start id should be an integer."));
  }

  const auto& slice_table = storage_->slice_table();
  int64_t start_val = arguments[0].long_value;
  switch (type_) {
    case Type::kSlice: {
      SliceId start_id(static_cast<uint32_t>(start_val));
      if (!GetDescendantsInternal(slice_table, slice_cursor_, start_id,
                                  descendants_, status_)) {
        return false;
      }
      break;
    }
    case Type::kSliceByStack:
      stack_cursor_.SetFilterValueUnchecked(0, start_val);
      stack_cursor_.Execute();
      for (; !stack_cursor_.Eof(); stack_cursor_.Next()) {
        if (!GetDescendantsInternal(slice_table, slice_cursor_,
                                    stack_cursor_.id(), descendants_,
                                    status_)) {
          return false;
        }
      }
      // Sort to keep the slices in timestamp order, similar to Ancestor.
      std::sort(descendants_.begin(), descendants_.end());
      break;
  }
  for (const auto& descendant_row : descendants_) {
    auto ref = descendant_row.ToRowReference(slice_table);
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

Descendant::Descendant(Type type, TraceStorage* storage)
    : type_(type), storage_(storage) {}

std::unique_ptr<StaticTableFunction::Cursor> Descendant::MakeCursor() {
  return std::make_unique<Cursor>(type_, storage_);
}

dataframe::DataframeSpec Descendant::CreateSpec() {
  return tables::SliceSubsetTable::kSpec.ToUntypedDataframeSpec();
}

std::string Descendant::TableName() {
  switch (type_) {
    case Type::kSlice:
      return "descendant_slice";
    case Type::kSliceByStack:
      return "descendant_slice_by_stack";
  }
  PERFETTO_FATAL("For GCC");
}

uint32_t Descendant::GetArgumentCount() const {
  return 1;
}
uint32_t Descendant::EstimateRowCount() {
  return 1;
}

tables::SliceTable::ConstCursor Descendant::MakeCursor(
    const tables::SliceTable& slices) {
  // As an optimization, for any finished slices, we only need to consider
  // slices which started before the end of this slice (because slices on a
  // track are always perfectly stacked).
  return slices.CreateCursor({
      dataframe::FilterSpec{
          tables::SliceTable::ColumnIndex::ts,
          0,
          dataframe::Ge{},
          std::nullopt,
      },
      dataframe::FilterSpec{
          tables::SliceTable::ColumnIndex::track_id,
          1,
          dataframe::Eq{},
          std::nullopt,
      },
      dataframe::FilterSpec{
          tables::SliceTable::ColumnIndex::depth,
          2,
          dataframe::Gt{},
          std::nullopt,
      },
      dataframe::FilterSpec{
          tables::SliceTable::ColumnIndex::ts,
          3,
          dataframe::Le{},
          std::nullopt,
      },
  });
}

// static
bool Descendant::GetDescendantSlices(
    const tables::SliceTable& slices,
    tables::SliceTable::ConstCursor& cursor,
    SliceId slice_id,
    std::vector<tables::SliceTable::RowNumber>& ret,
    base::Status& out_status) {
  return GetDescendantsInternal(slices, cursor, slice_id, ret, out_status);
}

}  // namespace perfetto::trace_processor
