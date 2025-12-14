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

#include "src/trace_processor/perfetto_sql/engine/static_table_function_module.h"

#include <sqlite3.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "src/trace_processor/dataframe/cursor_impl.h"  // IWYU pragma: keep
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/engine/dataframe_module.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/module_state_manager.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/tp_metatrace.h"

namespace perfetto::trace_processor {

namespace {

std::string ToSqliteCreateTableType(dataframe::StorageType type) {
  switch (type.index()) {
    case dataframe::StorageType::GetTypeIndex<dataframe::Id>():
    case dataframe::StorageType::GetTypeIndex<dataframe::Uint32>():
    case dataframe::StorageType::GetTypeIndex<dataframe::Int32>():
    case dataframe::StorageType::GetTypeIndex<dataframe::Int64>():
      return "INTEGER";
    case dataframe::StorageType::GetTypeIndex<dataframe::Double>():
      return "DOUBLE";
    case dataframe::StorageType::GetTypeIndex<dataframe::String>():
      return "TEXT";
    default:
      PERFETTO_FATAL("Unimplemented");
  }
}

std::string CreateTableStmt(uint32_t args_count,
                            const dataframe::DataframeSpec& spec) {
  std::string create_stmt = "CREATE TABLE x(";
  for (uint32_t i = 0; i < spec.column_specs.size(); ++i) {
    create_stmt += spec.column_names[i] + " " +
                   ToSqliteCreateTableType(spec.column_specs[i].type);
    create_stmt += ", ";
  }
  for (uint32_t i = 0; i < args_count; ++i) {
    create_stmt += "_fn_arg" + std::to_string(i) + " HIDDEN, ";
  }
  create_stmt += "_auto_id HIDDEN INTEGER NOT NULL, ";
  create_stmt += "PRIMARY KEY(_auto_id)) WITHOUT ROWID";
  return create_stmt;
}

}  // namespace

int StaticTableFunctionModule::Create(sqlite3* db,
                                      void* raw_ctx,
                                      int argc,
                                      const char* const* argv,
                                      sqlite3_vtab** vtab,
                                      char** err) {
  PERFETTO_CHECK(argc == 3);

  auto* ctx = GetContext(raw_ctx);
  auto state = std::move(ctx->temporary_create_state);
  PERFETTO_CHECK(state);

  uint32_t args_count = state->function->GetArgumentCount();
  auto spec = state->function->CreateSpec();
  std::string create_stmt = CreateTableStmt(args_count, spec);
  if (int r = sqlite3_declare_vtab(db, create_stmt.c_str()); r != SQLITE_OK) {
    *err = sqlite3_mprintf("failed to declare vtab %s", create_stmt.c_str());
    return r;
  }
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->output_count = static_cast<uint32_t>(spec.column_specs.size());
  res->arg_count = args_count;
  res->function = state->function.get();
  res->state = ctx->OnCreate(argc, argv, std::move(state));
  res->name = argv[2];
  *vtab = res.release();
  return SQLITE_OK;
}

int StaticTableFunctionModule::Destroy(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> v(GetVtab(vtab));
  sqlite::ModuleStateManager<StaticTableFunctionModule>::OnDestroy(v->state);
  return SQLITE_OK;
}

int StaticTableFunctionModule::Connect(sqlite3* db,
                                       void* raw_ctx,
                                       int argc,
                                       const char* const* argv,
                                       sqlite3_vtab** vtab,
                                       char**) {
  PERFETTO_CHECK(argc == 3);

  auto* vtab_state = GetContext(raw_ctx)->OnConnect(argc, argv);
  auto* state = sqlite::ModuleStateManager<StaticTableFunctionModule>::GetState(
      vtab_state);
  uint32_t args_count = state->function->GetArgumentCount();
  auto spec = state->function->CreateSpec();
  std::string create_stmt = CreateTableStmt(args_count, spec);
  if (int r = sqlite3_declare_vtab(db, create_stmt.c_str()); r != SQLITE_OK) {
    return r;
  }
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->output_count = static_cast<uint32_t>(spec.column_specs.size());
  res->arg_count = args_count;
  res->function = state->function.get();
  res->state = vtab_state;
  res->name = argv[2];
  *vtab = res.release();
  return SQLITE_OK;
}

int StaticTableFunctionModule::Disconnect(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> v(GetVtab(vtab));
  return SQLITE_OK;
}

int StaticTableFunctionModule::BestIndex(sqlite3_vtab* tab,
                                         sqlite3_index_info* info) {
  auto* v = GetVtab(tab);

  base::Status status = sqlite::utils::ValidateFunctionArguments(
      info, v->arg_count, [o = v->output_count](uint32_t i) { return i >= o; });
  if (!status.ok()) {
    // TODO(lalitm): instead of returning SQLITE_CONSTRAINT which shows the
    // user a very cryptic error message, consider instead SQLITE_OK but
    // with a very high (~infinite) cost. If SQLite still chose the query
    // plan after that, we can throw a proper error message in xFilter.
    return SQLITE_CONSTRAINT;
  }
  info->needToFreeIdxStr = true;
  info->idxNum = v->best_idx_num++;
  PERFETTO_TP_TRACE(metatrace::Category::QUERY_TIMELINE,
                    "STATIC_TABLE_FUNCTION_BEST_INDEX");
  return SQLITE_OK;
}

int StaticTableFunctionModule::Open(sqlite3_vtab* t,
                                    sqlite3_vtab_cursor** cursor) {
  std::unique_ptr<Cursor> c = std::make_unique<Cursor>();
  c->cursor = GetVtab(t)->function->MakeCursor();
  *cursor = c.release();
  return SQLITE_OK;
}

int StaticTableFunctionModule::Close(sqlite3_vtab_cursor* cursor) {
  std::unique_ptr<Cursor> c(GetCursor(cursor));
  return SQLITE_OK;
}

int StaticTableFunctionModule::Filter(sqlite3_vtab_cursor* cur,
                                      int,
                                      const char*,
                                      int argc,
                                      sqlite3_value** argv) {
  auto* c = GetCursor(cur);
  auto argc_size = static_cast<size_t>(argc);
  c->values.resize(argc_size);
  for (size_t i = 0; i < argc_size; ++i) {
    c->values[i] = sqlite::utils::SqliteValueToSqlValue(argv[i]);
  }
  if (!c->cursor->Run(c->values)) {
    return sqlite::utils::SetError(cur->pVtab, c->cursor->status());
  }
  SQLITE_ASSIGN_OR_RETURN(
      cur->pVtab, auto plan,
      c->cursor->dataframe()->PlanQuery(c->filters, {}, {}, {},
                                        std::numeric_limits<uint64_t>::max()));
  c->cursor->dataframe()->PrepareCursor(plan, c->df_cursor);
  DataframeModule::SqliteValueFetcher fetcher{{}, {}, nullptr};
  c->df_cursor.Execute(fetcher);
  return SQLITE_OK;
}

int StaticTableFunctionModule::Next(sqlite3_vtab_cursor* cur) {
  GetCursor(cur)->df_cursor.Next();
  return SQLITE_OK;
}

int StaticTableFunctionModule::Eof(sqlite3_vtab_cursor* cur) {
  return GetCursor(cur)->df_cursor.Eof();
}

int StaticTableFunctionModule::Column(sqlite3_vtab_cursor* cur,
                                      sqlite3_context* ctx,
                                      int raw_n) {
  auto* c = GetCursor(cur);
  auto* t = GetVtab(cur->pVtab);
  auto idx = static_cast<size_t>(raw_n);

  if (PERFETTO_LIKELY(idx < t->output_count)) {
    DataframeModule::SqliteResultCallback visitor{{}, ctx};
    c->df_cursor.Cell(static_cast<uint32_t>(raw_n), visitor);
  } else if (PERFETTO_LIKELY(idx < t->output_count + t->arg_count)) {
    // TODO(lalitm): it may be more appropriate to keep a note of the arguments
    // which we passed in and return them here. Not doing this to because it
    // doesn't seem necessary for any useful thing but something which may need
    // to be changed in the future.
    sqlite::result::Null(ctx);
  } else {
    PERFETTO_DCHECK(idx == t->output_count + t->arg_count);
    sqlite::result::Long(ctx, c->df_cursor.RowIndex());
  }
  return SQLITE_OK;
}

int StaticTableFunctionModule::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor
