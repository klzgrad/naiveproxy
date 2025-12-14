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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_EXPERIMENTAL_FLAMEGRAPH_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_EXPERIMENTAL_FLAMEGRAPH_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/flamegraph_construction_algorithms.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

class ExperimentalFlamegraph : public StaticTableFunction {
 public:
  enum class ProfileType : uint8_t { kGraph, kHeapProfile, kPerf };

  struct InputValues {
    ProfileType profile_type;
    std::optional<int64_t> ts;
    std::vector<TimeConstraints> time_constraints;
    std::optional<UniquePid> upid;
    std::optional<std::string> upid_group;
    std::optional<std::string> focus_str;
  };

  class Cursor : public StaticTableFunction::Cursor {
   public:
    explicit Cursor(TraceProcessorContext* context);
    bool Run(const std::vector<SqlValue>& arguments) override;

   private:
    TraceProcessorContext* context_ = nullptr;
    tables::ExperimentalFlamegraphTable table_;
  };

  explicit ExperimentalFlamegraph(TraceProcessorContext* context);
  virtual ~ExperimentalFlamegraph() override;

  std::unique_ptr<StaticTableFunction::Cursor> MakeCursor() override;
  dataframe::DataframeSpec CreateSpec() override;
  std::string TableName() override;
  uint32_t GetArgumentCount() const override;

 private:
  TraceProcessorContext* context_ = nullptr;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_EXPERIMENTAL_FLAMEGRAPH_H_
