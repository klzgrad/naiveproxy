/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_STDLIB_DOCS_TABLE_FUNCTION_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_STDLIB_DOCS_TABLE_FUNCTION_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/tables_py.h"

namespace perfetto::trace_processor {

class PerfettoSqlEngine;

// __intrinsic_stdlib_modules() — lists all (module, package) pairs.
class StdlibDocsModules : public StaticTableFunction {
 public:
  class Cursor : public StaticTableFunction::Cursor {
   public:
    explicit Cursor(StringPool*, const PerfettoSqlEngine*);
    bool Run(const std::vector<SqlValue>& arguments) override;

   private:
    StringPool* string_pool_ = nullptr;
    const PerfettoSqlEngine* engine_ = nullptr;
    tables::StdlibDocsModulesTable table_;
  };

  explicit StdlibDocsModules(StringPool*, const PerfettoSqlEngine*);
  std::unique_ptr<StaticTableFunction::Cursor> MakeCursor() override;
  dataframe::DataframeSpec CreateSpec() override;
  std::string TableName() override;
  uint32_t GetArgumentCount() const override;

 private:
  StringPool* string_pool_ = nullptr;
  const PerfettoSqlEngine* engine_ = nullptr;
};

// __intrinsic_stdlib_tables(module) — table/view metadata for a module.
class StdlibDocsTables : public StaticTableFunction {
 public:
  class Cursor : public StaticTableFunction::Cursor {
   public:
    explicit Cursor(StringPool*, const PerfettoSqlEngine*);
    bool Run(const std::vector<SqlValue>& arguments) override;

   private:
    StringPool* string_pool_ = nullptr;
    const PerfettoSqlEngine* engine_ = nullptr;
    tables::StdlibDocsTablesTable table_;
  };

  explicit StdlibDocsTables(StringPool*, const PerfettoSqlEngine*);
  std::unique_ptr<StaticTableFunction::Cursor> MakeCursor() override;
  dataframe::DataframeSpec CreateSpec() override;
  std::string TableName() override;
  uint32_t GetArgumentCount() const override;

 private:
  StringPool* string_pool_ = nullptr;
  const PerfettoSqlEngine* engine_ = nullptr;
};

// __intrinsic_stdlib_functions(module) — function metadata for a module.
class StdlibDocsFunctions : public StaticTableFunction {
 public:
  class Cursor : public StaticTableFunction::Cursor {
   public:
    explicit Cursor(StringPool*, const PerfettoSqlEngine*);
    bool Run(const std::vector<SqlValue>& arguments) override;

   private:
    StringPool* string_pool_ = nullptr;
    const PerfettoSqlEngine* engine_ = nullptr;
    tables::StdlibDocsFunctionsTable table_;
  };

  explicit StdlibDocsFunctions(StringPool*, const PerfettoSqlEngine*);
  std::unique_ptr<StaticTableFunction::Cursor> MakeCursor() override;
  dataframe::DataframeSpec CreateSpec() override;
  std::string TableName() override;
  uint32_t GetArgumentCount() const override;

 private:
  StringPool* string_pool_ = nullptr;
  const PerfettoSqlEngine* engine_ = nullptr;
};

// __intrinsic_stdlib_macros(module) — macro metadata for a module.
class StdlibDocsMacros : public StaticTableFunction {
 public:
  class Cursor : public StaticTableFunction::Cursor {
   public:
    explicit Cursor(StringPool*, const PerfettoSqlEngine*);
    bool Run(const std::vector<SqlValue>& arguments) override;

   private:
    StringPool* string_pool_ = nullptr;
    const PerfettoSqlEngine* engine_ = nullptr;
    tables::StdlibDocsMacrosTable table_;
  };

  explicit StdlibDocsMacros(StringPool*, const PerfettoSqlEngine*);
  std::unique_ptr<StaticTableFunction::Cursor> MakeCursor() override;
  dataframe::DataframeSpec CreateSpec() override;
  std::string TableName() override;
  uint32_t GetArgumentCount() const override;

 private:
  StringPool* string_pool_ = nullptr;
  const PerfettoSqlEngine* engine_ = nullptr;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_TABLE_FUNCTIONS_STDLIB_DOCS_TABLE_FUNCTION_H_
