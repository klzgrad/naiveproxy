/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/table_info.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/tables_py.h"

namespace perfetto::trace_processor {

namespace {

using TableInfoTable = tables::PerfettoTableInfoTable;

std::vector<TableInfoTable::Row> GetColInfoRows(const dataframe::Dataframe* df,
                                                StringPool* pool) {
  auto spec = df->CreateSpec();
  std::vector<TableInfoTable::Row> rows;
  for (uint32_t i = 0; i < spec.column_specs.size(); ++i) {
    TableInfoTable::Row row;
    row.name = pool->InternString(spec.column_names[i].c_str());

    const auto& col_spec = spec.column_specs[i];
    switch (col_spec.type.index()) {
      case dataframe::StorageType::GetTypeIndex<dataframe::String>():
        row.col_type = pool->InternString("string");
        break;
      case dataframe::StorageType::GetTypeIndex<dataframe::Int64>():
        row.col_type = pool->InternString("int64");
        break;
      case dataframe::StorageType::GetTypeIndex<dataframe::Int32>():
        row.col_type = pool->InternString("int32");
        break;
      case dataframe::StorageType::GetTypeIndex<dataframe::Uint32>():
        row.col_type = pool->InternString("uint32");
        break;
      case dataframe::StorageType::GetTypeIndex<dataframe::Double>():
        row.col_type = pool->InternString("double");
        break;
      case dataframe::StorageType::GetTypeIndex<dataframe::Id>():
        row.col_type = pool->InternString("id");
        break;
    }
    row.nullable = col_spec.nullability.index();
    row.sorted = col_spec.sort_state.index();
    rows.push_back(row);
  }
  return rows;
}

}  // namespace

TableInfo::Cursor::Cursor(StringPool* string_pool,
                          const PerfettoSqlEngine* engine)
    : string_pool_(string_pool), engine_(engine), table_(string_pool) {}

bool TableInfo::Cursor::Run(const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 1);

  if (arguments[0].type != SqlValue::kString) {
    return OnFailure(
        base::ErrStatus("perfetto_table_info takes table name as a string."));
  }

  table_.Clear();

  std::string table_name_str = arguments[0].AsString();
  auto table_name_id = string_pool_->InternString(table_name_str.c_str());

  if (const auto* df = engine_->GetDataframeOrNull(table_name_str); df) {
    for (auto& row : GetColInfoRows(df, string_pool_)) {
      row.table_name = table_name_id;
      table_.Insert(row);
    }
    return OnSuccess(&table_.dataframe());
  }
  return OnFailure(base::ErrStatus("Perfetto table '%s' not found.",
                                   table_name_str.c_str()));
}

TableInfo::TableInfo(StringPool* string_pool, const PerfettoSqlEngine* engine)
    : string_pool_(string_pool), engine_(engine) {}

std::unique_ptr<StaticTableFunction::Cursor> TableInfo::MakeCursor() {
  return std::make_unique<Cursor>(string_pool_, engine_);
}

dataframe::DataframeSpec TableInfo::CreateSpec() {
  return TableInfoTable::kSpec.ToUntypedDataframeSpec();
}

std::string TableInfo::TableName() {
  return TableInfoTable::Name();
}

uint32_t TableInfo::GetArgumentCount() const {
  return 1;
}

}  // namespace perfetto::trace_processor
