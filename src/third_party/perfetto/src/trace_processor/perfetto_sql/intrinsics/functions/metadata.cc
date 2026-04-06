// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/trace_processor/perfetto_sql/intrinsics/functions/metadata.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"

namespace perfetto::trace_processor {
namespace {

void ReportValue(sqlite3_context* ctx,
                 const TraceStorage* storage,
                 const tables::MetadataTable::ConstRowReference& rr) {
  if (rr.str_value().has_value()) {
    sqlite::result::StaticString(ctx,
                                 storage->GetString(*rr.str_value()).c_str());
  } else if (rr.int_value().has_value()) {
    sqlite::result::Long(ctx, *rr.int_value());
  } else {
    sqlite::result::Null(ctx);
  }
}

}  // namespace

// static
void ExtractMetadata::Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
  if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull) {
    return;
  }
  const char* name = sqlite::value::Text(argv[0]);
  auto* user_data = GetUserData(ctx);

  auto& cursor = user_data->cursor;
  cursor.SetFilterValueUnchecked(0, name);
  cursor.Execute();
  if (!cursor.Eof()) {
    ReportValue(ctx, user_data->storage,
                user_data->storage
                    ->metadata_table()[cursor.ToRowNumber().row_number()]);
  }
}

// static
void ExtractMetadataForMachine::Step(sqlite3_context* ctx,
                                     int,
                                     sqlite3_value** argv) {
  if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull ||
      sqlite::value::Type(argv[1]) == sqlite::Type::kNull) {
    return;
  }
  auto machine_id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
  const char* name = sqlite::value::Text(argv[1]);

  auto* user_data = GetUserData(ctx);
  auto& cursor = user_data->cursor;
  cursor.SetFilterValueUnchecked(0, name);
  cursor.SetFilterValueUnchecked(1, machine_id);
  cursor.Execute();
  if (!cursor.Eof()) {
    ReportValue(ctx, user_data->storage,
                user_data->storage
                    ->metadata_table()[cursor.ToRowNumber().row_number()]);
  }
}

// static
void ExtractMetadataForTrace::Step(sqlite3_context* ctx,
                                   int,
                                   sqlite3_value** argv) {
  if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull ||
      sqlite::value::Type(argv[1]) == sqlite::Type::kNull) {
    return;
  }
  auto trace_id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
  const char* name = sqlite::value::Text(argv[1]);

  auto* user_data = GetUserData(ctx);
  auto& cursor = user_data->cursor;
  cursor.SetFilterValueUnchecked(0, name);
  cursor.SetFilterValueUnchecked(1, trace_id);
  cursor.Execute();
  if (!cursor.Eof()) {
    ReportValue(ctx, user_data->storage,
                user_data->storage
                    ->metadata_table()[cursor.ToRowNumber().row_number()]);
  }
}

// static
void ExtractExactMetadata::Step(sqlite3_context* ctx,
                                int,
                                sqlite3_value** argv) {
  if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull ||
      sqlite::value::Type(argv[1]) == sqlite::Type::kNull ||
      sqlite::value::Type(argv[2]) == sqlite::Type::kNull) {
    return;
  }
  auto machine_id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
  auto trace_id = static_cast<uint32_t>(sqlite::value::Int64(argv[1]));
  const char* name = sqlite::value::Text(argv[2]);

  auto* user_data = GetUserData(ctx);
  auto& cursor = user_data->cursor;
  cursor.SetFilterValueUnchecked(0, name);
  cursor.SetFilterValueUnchecked(1, machine_id);
  cursor.SetFilterValueUnchecked(2, trace_id);
  cursor.Execute();
  if (!cursor.Eof()) {
    ReportValue(ctx, user_data->storage,
                user_data->storage
                    ->metadata_table()[cursor.ToRowNumber().row_number()]);
  }
}

base::Status RegisterMetadataFunctions(PerfettoSqlEngine& engine,
                                       TraceStorage* storage) {
  RETURN_IF_ERROR(engine.RegisterFunction<ExtractMetadata>(
      std::make_unique<ExtractMetadata::Context>(storage)));
  RETURN_IF_ERROR(engine.RegisterFunction<ExtractMetadataForMachine>(
      std::make_unique<ExtractMetadataForMachine::Context>(storage)));
  RETURN_IF_ERROR(engine.RegisterFunction<ExtractMetadataForTrace>(
      std::make_unique<ExtractMetadataForTrace::Context>(storage)));
  return engine.RegisterFunction<ExtractExactMetadata>(
      std::make_unique<ExtractExactMetadata::Context>(storage));
}

}  // namespace perfetto::trace_processor
