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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_DESCENDANT_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_DESCENDANT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/tables_py.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Implements the following dynamic tables:
// * descendant_slice
// * descendant_slice_by_stack
//
// See docs/analysis/trace-processor for usage.
class Descendant : public StaticTableFunction {
 public:
  enum class Type : uint8_t { kSlice = 1, kSliceByStack = 2 };
  class Cursor : public StaticTableFunction::Cursor {
   public:
    Cursor(Type type, TraceStorage* storage);
    bool Run(const std::vector<SqlValue>& arguments) override;

   private:
    Type type_;
    TraceStorage* storage_ = nullptr;
    tables::SliceSubsetTable table_;
    std::vector<tables::SliceTable::RowNumber> descendants_;
    tables::SliceTable::ConstCursor slice_cursor_;
    tables::SliceTable::ConstCursor stack_cursor_;
  };

  Descendant(Type type, TraceStorage* storage);

  std::unique_ptr<StaticTableFunction::Cursor> MakeCursor() override;
  dataframe::DataframeSpec CreateSpec() override;
  std::string TableName() override;
  uint32_t GetArgumentCount() const override;

  static tables::SliceTable::ConstCursor MakeCursor(const tables::SliceTable&);

  // Returns a vector of slice rows which are descendants of |slice_id|.
  // Returns false if an invalid |slice_id| is given or another error occurs.
  // This is used by ConnectedFlow to traverse flow indirectly connected flow
  // events.
  static bool GetDescendantSlices(const tables::SliceTable&,
                                  tables::SliceTable::ConstCursor&,
                                  SliceId slice_id,
                                  std::vector<tables::SliceTable::RowNumber>&,
                                  base::Status&);

 private:
  Type type_;
  TraceStorage* storage_ = nullptr;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_DESCENDANT_H_
