/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/engine/runtime_table_function.h"

#include <sqlite3.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/parser/function_util.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/module_state_manager.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/tp_metatrace.h"
#include "src/trace_processor/util/sql_argument.h"

namespace perfetto::trace_processor {

namespace {

void ResetStatement(sqlite3_stmt* stmt) {
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
}

auto CreateTableStrFromState(RuntimeTableFunctionModule::State* state) {
  std::vector<std::string> columns;
  columns.reserve(state->return_values.size());
  for (const auto& ret : state->return_values) {
    columns.emplace_back(ret.name().ToStdString() + " " +
                         sqlite::utils::SqlValueTypeToSqliteTypeName(
                             sql_argument::TypeToSqlValueType(ret.type())));
  }
  for (const auto& arg : state->prototype.arguments) {
    // Add the "in_" prefix to every argument param to avoid clashes between the
    // output and input parameters.
    columns.emplace_back("in_" + arg.name().ToStdString() + " " +
                         sqlite::utils::SqlValueTypeToSqliteTypeName(
                             sql_argument::TypeToSqlValueType(arg.type())) +
                         " HIDDEN");
  }
  columns.emplace_back("_primary_key BIGINT HIDDEN");

  std::string cols = base::Join(columns, ",");
  return base::StackString<1024>(
      R"(CREATE TABLE x(%s, PRIMARY KEY(_primary_key)) WITHOUT ROWID)",
      cols.c_str());
}

}  // namespace

int RuntimeTableFunctionModule::Create(sqlite3* db,
                                       void* ctx,
                                       int argc,
                                       const char* const* argv,
                                       sqlite3_vtab** vtab,
                                       char**) {
  auto* context = GetContext(ctx);
  auto state = std::move(context->temporary_create_state);

  auto create_table_str = CreateTableStrFromState(state.get());
  if (int ret = sqlite3_declare_vtab(db, create_table_str.c_str());
      ret != SQLITE_OK) {
    return ret;
  }
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->reusable_stmt = std::move(state->temporary_create_stmt);
  state->temporary_create_stmt = std::nullopt;
  res->state = context->OnCreate(argc, argv, std::move(state));
  *vtab = res.release();
  return SQLITE_OK;
}

int RuntimeTableFunctionModule::Destroy(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  sqlite::ModuleStateManager<RuntimeTableFunctionModule>::OnDestroy(tab->state);
  return SQLITE_OK;
}

int RuntimeTableFunctionModule::Connect(sqlite3* db,
                                        void* ctx,
                                        int argc,
                                        const char* const*,
                                        sqlite3_vtab** vtab,
                                        char** argv) {
  auto* context = GetContext(ctx);

  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->state = context->OnConnect(argc, argv);

  auto create_table_str = CreateTableStrFromState(
      sqlite::ModuleStateManager<RuntimeTableFunctionModule>::GetState(
          res->state));
  if (int ret = sqlite3_declare_vtab(db, create_table_str.c_str());
      ret != SQLITE_OK) {
    return ret;
  }
  *vtab = res.release();
  return SQLITE_OK;
}

int RuntimeTableFunctionModule::Disconnect(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  return SQLITE_OK;
}

int RuntimeTableFunctionModule::BestIndex(sqlite3_vtab* tab,
                                          sqlite3_index_info* info) {
  auto* t = GetVtab(tab);
  auto* s = sqlite::ModuleStateManager<RuntimeTableFunctionModule>::GetState(
      t->state);

  // Don't deal with any constraints on the output parameters for simplicity.
  // TODO(lalitm): reconsider this decision to allow more efficient queries:
  // we would need to wrap the query in a SELECT * FROM (...) WHERE constraint
  // like we do for SPAN JOIN.
  base::Status status = sqlite::utils::ValidateFunctionArguments(
      info, s->prototype.arguments.size(),
      [s](size_t c) { return s->IsArgumentColumn(c); });
  if (!status.ok()) {
    return SQLITE_CONSTRAINT;
  }
  return SQLITE_OK;
}

int RuntimeTableFunctionModule::Open(sqlite3_vtab* tab,
                                     sqlite3_vtab_cursor** cursor) {
  auto* t = GetVtab(tab);
  std::unique_ptr<Cursor> c = std::make_unique<Cursor>();
  if (t->reusable_stmt) {
    c->stmt = std::move(t->reusable_stmt);
    t->reusable_stmt = std::nullopt;
  }
  *cursor = c.release();
  return SQLITE_OK;
}

int RuntimeTableFunctionModule::Close(sqlite3_vtab_cursor* cursor) {
  std::unique_ptr<Cursor> c(GetCursor(cursor));
  auto* t = GetVtab(c->pVtab);
  if (!t->reusable_stmt && c->stmt) {
    ResetStatement(c->stmt->sqlite_stmt());
    t->reusable_stmt = std::move(c->stmt);
  }
  return SQLITE_OK;
}

int RuntimeTableFunctionModule::Filter(sqlite3_vtab_cursor* cur,
                                       int,
                                       const char*,
                                       int argc,
                                       sqlite3_value** argv) {
  auto* c = GetCursor(cur);
  auto* t = GetVtab(cur->pVtab);
  auto* s = sqlite::ModuleStateManager<RuntimeTableFunctionModule>::GetState(
      t->state);

  PERFETTO_CHECK(static_cast<size_t>(argc) == s->prototype.arguments.size());
  PERFETTO_TP_TRACE(metatrace::Category::FUNCTION_CALL, "TABLE_FUNCTION_CALL",
                    [s](metatrace::Record* r) {
                      r->AddArg("Function", s->prototype.function_name.c_str());
                    });

  // Prepare the SQL definition as a statement using SQLite.
  // TODO(lalitm): measure and implement whether it would be a good idea to
  // forward constraints here when we build the nested query.
  if (c->stmt) {
    // Filter can be called multiple times for the same cursor, so if we
    // already have a statement, reset and reuse it. Otherwise, create a
    // new one.
    ResetStatement(c->stmt->sqlite_stmt());
  } else {
    auto stmt = s->engine->sqlite_engine()->PrepareStatement(s->sql_defn_str);
    c->stmt = std::move(stmt);
    if (const auto& status = c->stmt->status(); !status.ok()) {
      return sqlite::utils::SetError(t, status.c_message());
    }
  }

  // Bind all the arguments to the appropriate places in the function.
  for (uint32_t i = 0; i < static_cast<uint32_t>(argc); ++i) {
    const auto& arg = s->prototype.arguments[i];
    base::Status status = MaybeBindArgument(
        c->stmt->sqlite_stmt(), s->prototype.function_name, arg, argv[i]);
    if (!status.ok()) {
      return sqlite::utils::SetError(t, status.c_message());
    }
  }

  // Reset the next call count - this is necessary because the same cursor
  // can be used for multiple filter operations.
  c->next_call_count = 0;
  return Next(cur);
}

int RuntimeTableFunctionModule::Next(sqlite3_vtab_cursor* cur) {
  auto* c = GetCursor(cur);
  c->is_eof = !c->stmt->Step();
  c->next_call_count++;
  if (const auto& status = c->stmt->status(); !status.ok()) {
    return sqlite::utils::SetError(cur->pVtab, status.c_message());
  }
  return SQLITE_OK;
}

int RuntimeTableFunctionModule::Eof(sqlite3_vtab_cursor* cur) {
  return GetCursor(cur)->is_eof;
}

int RuntimeTableFunctionModule::Column(sqlite3_vtab_cursor* cur,
                                       sqlite3_context* ctx,
                                       int N) {
  auto* c = GetCursor(cur);
  auto* t = GetVtab(cur->pVtab);
  auto* s = sqlite::ModuleStateManager<RuntimeTableFunctionModule>::GetState(
      t->state);

  auto idx = static_cast<size_t>(N);
  if (PERFETTO_LIKELY(s->IsReturnValueColumn(idx))) {
    sqlite::result::Value(ctx, sqlite3_column_value(c->stmt->sqlite_stmt(), N));
    return SQLITE_OK;
  }

  if (PERFETTO_LIKELY(s->IsArgumentColumn(idx))) {
    // TODO(lalitm): it may be more appropriate to keep a note of the arguments
    // which we passed in and return them here. Not doing this to because it
    // doesn't seem necessary for any useful thing but something which may need
    // to be changed in the future.
    sqlite::result::Null(ctx);
    return SQLITE_OK;
  }

  PERFETTO_DCHECK(s->IsPrimaryKeyColumn(idx));
  sqlite::result::Long(ctx, c->next_call_count);
  return SQLITE_OK;
}

int RuntimeTableFunctionModule::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor
