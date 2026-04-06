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
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/tables_py.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {
namespace {

// Walks the parent chain of |candidate| to check whether |ancestor_id| is an
// ancestor. Stops early when the depth drops to |ancestor_depth| or below.
bool IsAncestor(const tables::SliceTable& slices,
                tables::SliceTable::ConstRowReference candidate,
                SliceId ancestor_id,
                uint32_t ancestor_depth) {
  for (auto id = candidate.parent_id(); id;) {
    if (*id == ancestor_id) {
      return true;
    }
    auto ref = slices.FindById(*id);
    if (!ref || ref->depth() <= ancestor_depth) {
      return false;
    }
    id = ref->parent_id();
  }
  return false;
}

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
  // Intervals are closed on the left and open on the right, so we use Lt for
  // the upper bound. However, instants (dur=0) stack on top of each other, so
  // for an instant at ts=T we need child_ts <= T, achieved by using T+1 as
  // the Lt bound. See SliceTracker::TryCloseStack for the matching logic.
  int64_t ts_upper_bound;
  if (start_ref->dur() > 0) {
    ts_upper_bound = start_ref->ts() + start_ref->dur();
  } else if (start_ref->dur() == 0) {
    ts_upper_bound = start_ref->ts() + 1;
  } else {
    ts_upper_bound = std::numeric_limits<int64_t>::max();
  }
  cursor.SetFilterValueUnchecked(3, ts_upper_bound);

  // The timestamp filter can produce false positives at the start boundary
  // (candidate.ts == start.ts) where a child of a slice ending at start.ts
  // shares the same timestamp. For such candidates, walk the parent chain to
  // verify ancestry. For candidates strictly inside the interval (ts >
  // start.ts), same-depth non-overlapping guarantees they are true descendants.
  int64_t start_ts = start_ref->ts();
  for (cursor.Execute(); !cursor.Eof(); cursor.Next()) {
    auto row_num = cursor.ToRowNumber();
    auto ref = row_num.ToRowReference(slices);
    if (ref.ts() == start_ts &&
        !IsAncestor(slices, ref, starting_id, start_ref->depth())) {
      continue;
    }
    row_numbers_accumulator.emplace_back(row_num);
  }
  return true;
}

}  // namespace

Descendant::Cursor::Cursor(Type type, TraceStorage* storage)
    : type_(type),
      storage_(storage),
      table_(storage->mutable_string_pool()),
      slice_cursor_(MakeCursor(storage->slice_table())) {}

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
  }
  PERFETTO_FATAL("For GCC");
}

uint32_t Descendant::GetArgumentCount() const {
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
          dataframe::Lt{},
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
