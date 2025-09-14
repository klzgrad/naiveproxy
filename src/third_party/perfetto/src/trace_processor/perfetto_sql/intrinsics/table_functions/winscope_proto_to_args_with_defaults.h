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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_WINSCOPE_PROTO_TO_ARGS_WITH_DEFAULTS_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_WINSCOPE_PROTO_TO_ARGS_WITH_DEFAULTS_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/tables_py.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

class WinscopeProtoToArgsWithDefaults : public StaticTableFunction {
 public:
  class Cursor : public StaticTableFunction::Cursor {
   public:
    explicit Cursor(StringPool* string_pool,
                    const PerfettoSqlEngine* engine,
                    TraceProcessorContext* context);
    bool Run(const std::vector<SqlValue>& arguments) override;

   private:
    StringPool* string_pool_ = nullptr;
    const PerfettoSqlEngine* engine_ = nullptr;
    TraceProcessorContext* context_ = nullptr;
    tables::WinscopeArgsWithDefaultsTable table_;
  };

  explicit WinscopeProtoToArgsWithDefaults(StringPool*,
                                           const PerfettoSqlEngine*,
                                           TraceProcessorContext* context);

  std::unique_ptr<StaticTableFunction::Cursor> MakeCursor() override;
  dataframe::DataframeSpec CreateSpec() override;
  std::string TableName() override;
  uint32_t GetArgumentCount() const override;

 private:
  StringPool* string_pool_ = nullptr;
  const PerfettoSqlEngine* engine_ = nullptr;
  TraceProcessorContext* context_ = nullptr;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_WINSCOPE_PROTO_TO_ARGS_WITH_DEFAULTS_H_
