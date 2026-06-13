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

#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/stdlib_docs_table_function.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/tables_py.h"
#include "src/trace_processor/util/simple_json_serializer.h"
#include "src/trace_processor/util/sql_module_doc_parser.h"
#include "src/trace_processor/util/sql_modules.h"

namespace perfetto::trace_processor {

namespace {

template <typename Entry>
std::string SerializeEntries(const std::vector<Entry>& entries) {
  return json::SerializeJson([&](json::JsonValueSerializer&& writer) {
    std::move(writer).WriteArray([&](json::JsonArraySerializer& array) {
      for (const auto& e : entries) {
        array.AppendDict([&](json::JsonDictSerializer& dict) {
          dict.AddString("name", e.name);
          dict.AddString("type", e.type);
          dict.AddString("description", e.description);
        });
      }
    });
  });
}

base::StatusOr<stdlib_doc::ParsedModule> ParseModule(
    const PerfettoSqlEngine* engine,
    const std::string& module_key) {
  const auto* package =
      engine->FindPackage(sql_modules::GetPackageName(module_key));
  if (!package) {
    return base::ErrStatus("Module not found: %s", module_key.c_str());
  }
  const auto* mod = package->modules.Find(module_key);
  if (!mod) {
    return base::ErrStatus("Module not found: %s", module_key.c_str());
  }
  PERFETTO_DCHECK(mod->sql.size() <= std::numeric_limits<uint32_t>::max());
  auto parsed = stdlib_doc::ParseStdlibModule(
      mod->sql.c_str(), static_cast<uint32_t>(mod->sql.size()));
  for (const auto& err : parsed.errors) {
    PERFETTO_DLOG("stdlib docs: parse error in '%s': %s", module_key.c_str(),
                  err.c_str());
  }
  return parsed;
}

}  // namespace

// ============================================================================
// StdlibDocsModules
// ============================================================================

StdlibDocsModules::Cursor::Cursor(StringPool* pool,
                                  const PerfettoSqlEngine* engine)
    : string_pool_(pool), engine_(engine), table_(pool) {}

bool StdlibDocsModules::Cursor::Run(const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.empty());
  table_.Clear();
  for (const auto& [pkg, mod] : engine_->GetModules()) {
    tables::StdlibDocsModulesTable::Row row;
    row.module = string_pool_->InternString(base::StringView(mod));
    row.package = string_pool_->InternString(base::StringView(pkg));
    table_.Insert(row);
  }
  return OnSuccess(&table_.dataframe());
}

StdlibDocsModules::StdlibDocsModules(StringPool* pool,
                                     const PerfettoSqlEngine* engine)
    : string_pool_(pool), engine_(engine) {}

std::unique_ptr<StaticTableFunction::Cursor> StdlibDocsModules::MakeCursor() {
  return std::make_unique<Cursor>(string_pool_, engine_);
}

dataframe::DataframeSpec StdlibDocsModules::CreateSpec() {
  return tables::StdlibDocsModulesTable::kSpec.ToUntypedDataframeSpec();
}

std::string StdlibDocsModules::TableName() {
  return "__intrinsic_stdlib_modules";
}

uint32_t StdlibDocsModules::GetArgumentCount() const {
  return 0;
}

// ============================================================================
// StdlibDocsTables
// ============================================================================

StdlibDocsTables::Cursor::Cursor(StringPool* pool,
                                 const PerfettoSqlEngine* engine)
    : string_pool_(pool), engine_(engine), table_(pool) {}

bool StdlibDocsTables::Cursor::Run(const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 1);
  table_.Clear();
  if (arguments[0].is_null()) {
    return OnSuccess(&table_.dataframe());
  }
  if (arguments[0].type != SqlValue::kString) {
    return OnFailure(
        base::ErrStatus("__intrinsic_stdlib_tables: module must be a string"));
  }
  std::string module_key = arguments[0].AsString();
  auto parsed_or = ParseModule(engine_, module_key);
  if (!parsed_or.ok()) {
    return OnFailure(parsed_or.status());
  }
  for (const auto& tv : parsed_or->table_views) {
    tables::StdlibDocsTablesTable::Row row;
    row.name = string_pool_->InternString(base::StringView(tv.name));
    row.type = string_pool_->InternString(base::StringView(tv.type));
    row.description =
        string_pool_->InternString(base::StringView(tv.description));
    row.exposed = tv.exposed ? 1 : 0;
    row.cols = string_pool_->InternString(
        base::StringView(SerializeEntries(tv.columns)));
    table_.Insert(row);
  }
  return OnSuccess(&table_.dataframe());
}

StdlibDocsTables::StdlibDocsTables(StringPool* pool,
                                   const PerfettoSqlEngine* engine)
    : string_pool_(pool), engine_(engine) {}

std::unique_ptr<StaticTableFunction::Cursor> StdlibDocsTables::MakeCursor() {
  return std::make_unique<Cursor>(string_pool_, engine_);
}

dataframe::DataframeSpec StdlibDocsTables::CreateSpec() {
  return tables::StdlibDocsTablesTable::kSpec.ToUntypedDataframeSpec();
}

std::string StdlibDocsTables::TableName() {
  return "__intrinsic_stdlib_tables";
}

uint32_t StdlibDocsTables::GetArgumentCount() const {
  return 1;
}

// ============================================================================
// StdlibDocsFunctions
// ============================================================================

StdlibDocsFunctions::Cursor::Cursor(StringPool* pool,
                                    const PerfettoSqlEngine* engine)
    : string_pool_(pool), engine_(engine), table_(pool) {}

bool StdlibDocsFunctions::Cursor::Run(const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 1);
  table_.Clear();
  if (arguments[0].is_null()) {
    return OnSuccess(&table_.dataframe());
  }
  if (arguments[0].type != SqlValue::kString) {
    return OnFailure(base::ErrStatus(
        "__intrinsic_stdlib_functions: module must be a string"));
  }
  std::string module_key = arguments[0].AsString();
  auto parsed_or = ParseModule(engine_, module_key);
  if (!parsed_or.ok()) {
    return OnFailure(parsed_or.status());
  }
  for (const auto& fn : parsed_or->functions) {
    tables::StdlibDocsFunctionsTable::Row row;
    row.name = string_pool_->InternString(base::StringView(fn.name));
    row.description =
        string_pool_->InternString(base::StringView(fn.description));
    row.exposed = fn.exposed ? 1 : 0;
    row.is_table_function = fn.is_table_function ? 1 : 0;
    row.return_type =
        string_pool_->InternString(base::StringView(fn.return_type));
    row.return_description =
        string_pool_->InternString(base::StringView(fn.return_description));
    row.args =
        string_pool_->InternString(base::StringView(SerializeEntries(fn.args)));
    row.cols = string_pool_->InternString(
        base::StringView(SerializeEntries(fn.columns)));
    table_.Insert(row);
  }
  return OnSuccess(&table_.dataframe());
}

StdlibDocsFunctions::StdlibDocsFunctions(StringPool* pool,
                                         const PerfettoSqlEngine* engine)
    : string_pool_(pool), engine_(engine) {}

std::unique_ptr<StaticTableFunction::Cursor> StdlibDocsFunctions::MakeCursor() {
  return std::make_unique<Cursor>(string_pool_, engine_);
}

dataframe::DataframeSpec StdlibDocsFunctions::CreateSpec() {
  return tables::StdlibDocsFunctionsTable::kSpec.ToUntypedDataframeSpec();
}

std::string StdlibDocsFunctions::TableName() {
  return "__intrinsic_stdlib_functions";
}

uint32_t StdlibDocsFunctions::GetArgumentCount() const {
  return 1;
}

// ============================================================================
// StdlibDocsMacros
// ============================================================================

StdlibDocsMacros::Cursor::Cursor(StringPool* pool,
                                 const PerfettoSqlEngine* engine)
    : string_pool_(pool), engine_(engine), table_(pool) {}

bool StdlibDocsMacros::Cursor::Run(const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 1);
  table_.Clear();
  if (arguments[0].is_null()) {
    return OnSuccess(&table_.dataframe());
  }
  if (arguments[0].type != SqlValue::kString) {
    return OnFailure(
        base::ErrStatus("__intrinsic_stdlib_macros: module must be a string"));
  }
  std::string module_key = arguments[0].AsString();
  auto parsed_or = ParseModule(engine_, module_key);
  if (!parsed_or.ok()) {
    return OnFailure(parsed_or.status());
  }
  for (const auto& macro : parsed_or->macros) {
    tables::StdlibDocsMacrosTable::Row row;
    row.name = string_pool_->InternString(base::StringView(macro.name));
    row.description =
        string_pool_->InternString(base::StringView(macro.description));
    row.exposed = macro.exposed ? 1 : 0;
    row.return_type =
        string_pool_->InternString(base::StringView(macro.return_type));
    row.return_description =
        string_pool_->InternString(base::StringView(macro.return_description));
    row.args = string_pool_->InternString(
        base::StringView(SerializeEntries(macro.args)));
    table_.Insert(row);
  }
  return OnSuccess(&table_.dataframe());
}

StdlibDocsMacros::StdlibDocsMacros(StringPool* pool,
                                   const PerfettoSqlEngine* engine)
    : string_pool_(pool), engine_(engine) {}

std::unique_ptr<StaticTableFunction::Cursor> StdlibDocsMacros::MakeCursor() {
  return std::make_unique<Cursor>(string_pool_, engine_);
}

dataframe::DataframeSpec StdlibDocsMacros::CreateSpec() {
  return tables::StdlibDocsMacrosTable::kSpec.ToUntypedDataframeSpec();
}

std::string StdlibDocsMacros::TableName() {
  return "__intrinsic_stdlib_macros";
}

uint32_t StdlibDocsMacros::GetArgumentCount() const {
  return 1;
}

}  // namespace perfetto::trace_processor
