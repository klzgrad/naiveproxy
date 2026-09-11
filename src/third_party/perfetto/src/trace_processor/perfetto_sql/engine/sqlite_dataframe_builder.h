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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_SQLITE_DATAFRAME_BUILDER_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_SQLITE_DATAFRAME_BUILDER_H_

#include <sqlite3.h>

#include <string>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/core/dataframe/runtime_dataframe_builder.h"
#include "src/trace_processor/sqlite/sqlite_connection.h"

namespace perfetto::trace_processor {

struct SqliteDataframeBuilderOptions {
  std::vector<dataframe::AdhocDataframeBuilder::ColumnType> column_types;
  dataframe::NullabilityType nullability =
      dataframe::NullabilityType::kSparseNull;
  bool blobs_as_null = false;
};

// Consumes |stmt| into a runtime-typed dataframe. The statement must already
// have been stepped once, leaving it either on its first row or done.
base::StatusOr<dataframe::RuntimeDataframeBuilder>
BuildRuntimeDataframeFromSqliteStatement(
    StringPool* pool,
    std::vector<std::string> column_names,
    SqliteConnection::PreparedStatement* stmt,
    std::string_view error_context,
    SqliteDataframeBuilderOptions options = {});

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_SQLITE_DATAFRAME_BUILDER_H_
