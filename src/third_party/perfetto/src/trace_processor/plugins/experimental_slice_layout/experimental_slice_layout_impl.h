/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_EXPERIMENTAL_SLICE_LAYOUT_EXPERIMENTAL_SLICE_LAYOUT_IMPL_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_EXPERIMENTAL_SLICE_LAYOUT_EXPERIMENTAL_SLICE_LAYOUT_IMPL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/plugin/registration.h"
#include "src/trace_processor/plugins/experimental_slice_layout/tables_py.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"

namespace perfetto::trace_processor {

class ExperimentalSliceLayout : public StaticTableFunction {
 public:
  struct CachedRow {
    SliceId id;
    uint32_t layout_depth;
  };
  class Cursor : public StaticTableFunction::Cursor {
   public:
    explicit Cursor(
        StringPool* string_pool,
        const tables::SliceTable* table,
        std::unordered_map<StringPool::Id, std::vector<CachedRow>>* cache);
    bool Run(const std::vector<SqlValue>& arguments) override;

   private:
    std::vector<CachedRow> ComputeLayoutTable(
        const std::vector<tables::SliceTable::RowNumber>& rows);

    StringPool* string_pool_;
    const tables::SliceTable* slice_table_;
    tables::ExperimentalSliceLayoutTable table_;
    std::unordered_map<StringPool::Id, std::vector<CachedRow>>* cache_;
  };

  explicit ExperimentalSliceLayout(StringPool* string_pool,
                                   const tables::SliceTable* table);
  ~ExperimentalSliceLayout() override;

  std::unique_ptr<StaticTableFunction::Cursor> MakeCursor() override;
  dataframe::DataframeSpec CreateSpec() override;
  std::string TableName() override;
  uint32_t GetArgumentCount() const override;

 private:
  StringPool* string_pool_;
  const tables::SliceTable* slice_table_;
  std::unordered_map<StringPool::Id, std::vector<CachedRow>> cache_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_EXPERIMENTAL_SLICE_LAYOUT_EXPERIMENTAL_SLICE_LAYOUT_IMPL_H_
