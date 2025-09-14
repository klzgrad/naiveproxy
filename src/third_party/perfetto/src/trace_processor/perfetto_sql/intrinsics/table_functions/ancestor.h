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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_ANCESTOR_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_ANCESTOR_H_

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
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/tables/slice_tables_py.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Implements the following dynamic tables:
// * ancestor_slice
// * experimental_ancestor_stack_profile_callsite
// * ancestor_slice_by_stack
//
// See docs/analysis/trace-processor for usage.
class Ancestor : public StaticTableFunction {
 public:
  enum class Type : uint8_t {
    kSlice = 1,
    kSliceByStack = 2,
    kStackProfileCallsite = 3,
  };
  class SliceCursor : public StaticTableFunction::Cursor {
   public:
    SliceCursor(Type type, TraceStorage* storage);
    bool Run(const std::vector<SqlValue>& arguments) override;

   private:
    Type type_;
    TraceStorage* storage_ = nullptr;
    tables::SliceSubsetTable table_;
    std::vector<tables::SliceTable::RowNumber> ancestors_;
    tables::SliceTable::ConstCursor stack_cursor_;
  };
  class StackProfileCursor : public StaticTableFunction::Cursor {
   public:
    explicit StackProfileCursor(TraceStorage* storage);
    bool Run(const std::vector<SqlValue>& arguments) override;

   private:
    TraceStorage* storage_ = nullptr;
    tables::AncestorStackProfileCallsiteTable table_;
    std::vector<tables::StackProfileCallsiteTable::RowNumber> ancestors_;
  };

  Ancestor(Type type, TraceStorage* storage);

  std::unique_ptr<StaticTableFunction::Cursor> MakeCursor() override;
  dataframe::DataframeSpec CreateSpec() override;
  std::string TableName() override;
  uint32_t GetArgumentCount() const override;

  // Returns a vector of rows numbers which are ancestors of |slice_id|.
  // Returns std::nullopt if an invalid |slice_id| is given. This is used by
  // ConnectedFlow to traverse flow indirectly connected flow events.
  static bool GetAncestorSlices(const tables::SliceTable& slices,
                                SliceId slice_id,
                                std::vector<tables::SliceTable::RowNumber>&,
                                base::Status&);

 private:
  Type type_;
  TraceStorage* storage_ = nullptr;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_ANCESTOR_H_
