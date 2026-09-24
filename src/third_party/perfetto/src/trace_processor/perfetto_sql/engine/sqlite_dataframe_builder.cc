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

#include "src/trace_processor/perfetto_sql/engine/sqlite_dataframe_builder.h"

#include <cstdint>
#include <utility>

#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/core/dataframe/runtime_dataframe_builder.h"
#include "src/trace_processor/sqlite/bindings/sqlite_column.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"

namespace perfetto::trace_processor {
namespace {

struct SqliteValueFetcher : public dataframe::ValueFetcher {
  SqliteValueFetcher(sqlite3_stmt* sql_stmt, bool null_blobs)
      : stmt(sql_stmt), blobs_as_null(null_blobs) {}

  using Type = sqlite::Type;
  static constexpr Type kInt64 = sqlite::Type::kInteger;
  static constexpr Type kDouble = sqlite::Type::kFloat;
  static constexpr Type kString = sqlite::Type::kText;
  static constexpr Type kNull = sqlite::Type::kNull;
  static constexpr Type kBytes = sqlite::Type::kBlob;

  int64_t GetInt64Value(uint32_t column) const {
    return sqlite::column::Int64(stmt, column);
  }
  double GetDoubleValue(uint32_t column) const {
    return sqlite::column::Double(stmt, column);
  }
  const char* GetStringValue(uint32_t column) const {
    return sqlite::column::Text(stmt, column);
  }
  Type GetValueType(uint32_t column) const {
    Type type = sqlite::column::Type(stmt, column);
    return blobs_as_null && type == kBytes ? kNull : type;
  }

  sqlite3_stmt* stmt;
  bool blobs_as_null;
};

}  // namespace

base::StatusOr<dataframe::RuntimeDataframeBuilder>
BuildRuntimeDataframeFromSqliteStatement(
    StringPool* pool,
    std::vector<std::string> column_names,
    SqliteConnection::PreparedStatement* stmt,
    std::string_view error_context,
    SqliteDataframeBuilderOptions options) {
  dataframe::RuntimeDataframeBuilder builder(
      std::move(column_names), pool,
      {std::move(options.column_types), options.nullability});
  SqliteValueFetcher fetcher{stmt->sqlite_stmt(), options.blobs_as_null};
  while (!stmt->IsDone()) {
    if (!builder.AddRow(&fetcher)) {
      return base::ErrStatus("%.*s: %s", static_cast<int>(error_context.size()),
                             error_context.data(),
                             builder.status().c_message());
    }
    stmt->Step();
  }
  RETURN_IF_ERROR(stmt->status());
  return std::move(builder);
}

}  // namespace perfetto::trace_processor
