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

#include "src/trace_processor/sqlite/sqlite_engine.h"

#include <sqlite3.h>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/sqlite/scoped_db.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/tp_metatrace.h"

#include "protos/perfetto/trace_processor/metatrace_categories.pbzero.h"

// In Android and Chromium tree builds, we don't have the percentile module.
// Just don't include it.
#if PERFETTO_BUILDFLAG(PERFETTO_TP_PERCENTILE)
// defined in sqlite_src/ext/misc/percentile.c
extern "C" int sqlite3_percentile_init(sqlite3* db,
                                       char** error,
                                       const sqlite3_api_routines* api);
#endif  // PERFETTO_BUILDFLAG(PERFETTO_TP_PERCENTILE)

namespace perfetto::trace_processor {
namespace {

void EnsureSqliteInitialized() {
  // sqlite3_initialize isn't actually thread-safe in standalone builds because
  // we build with SQLITE_THREADSAFE=0. Ensure it's only called from a single
  // thread.
  static bool init_once = [] {
    // Enabling memstatus causes a lock to be taken on every malloc/free in
    // SQLite to update the memory statistics. This can cause massive contention
    // in trace processor when multiple instances are used in parallel.
    // Fix this by disabling the memstatus API which we don't make use of in
    // any case. See b/335019324 for more info on this.
    int ret = sqlite3_config(SQLITE_CONFIG_MEMSTATUS, 0);

    // As much as it is painful, we need to catch instances of SQLITE_MISUSE
    // here against all the advice of the SQLite developers and lalitm@'s
    // intuition: SQLITE_MISUSE for sqlite3_config really means: that someone
    // else has already initialized SQLite. As we are an embeddable library,
    // it's very possible that the process embedding us has initialized SQLite
    // in a different way to what we want to do and, if so, we should respect
    // their choice.
    //
    // TODO(lalitm): ideally we would have an sqlite3_is_initialized API we
    // could use to gate the above check but that doesn't exist: report this
    // issue to SQLite developers and see if such an API could be added. If so
    // we can remove this check.
    if (ret == SQLITE_MISUSE) {
      return true;
    }

    PERFETTO_CHECK(ret == SQLITE_OK);
    return sqlite3_initialize() == SQLITE_OK;
  }();
  PERFETTO_CHECK(init_once);
}

void InitializeSqlite(sqlite3* db) {
  char* error = nullptr;
  sqlite3_exec(db, "PRAGMA temp_store=2", nullptr, nullptr, &error);
  if (error) {
    PERFETTO_FATAL("Error setting pragma temp_store: %s", error);
  }
// In Android tree builds, we don't have the percentile module.
#if PERFETTO_BUILDFLAG(PERFETTO_TP_PERCENTILE)
  sqlite3_percentile_init(db, &error, nullptr);
  if (error) {
    PERFETTO_ELOG("Error initializing: %s", error);
    sqlite3_free(error);
  }
#endif
}

std::optional<uint32_t> GetErrorOffsetDb(sqlite3* db) {
  int offset = sqlite3_error_offset(db);
  return offset == -1 ? std::nullopt
                      : std::make_optional(static_cast<uint32_t>(offset));
}

}  // namespace

SqliteEngine::SqliteEngine() {
  sqlite3* db = nullptr;
  EnsureSqliteInitialized();

  // Ensure that we open the database with mutexes disabled: this is because
  // trace processor as a whole cannot be used from multiple threads so there is
  // no point paying the (potentially significant) cost of mutexes at the SQLite
  // level.
  static constexpr int kSqliteOpenFlags =
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
  PERFETTO_CHECK(sqlite3_open_v2(":memory:", &db, kSqliteOpenFlags, nullptr) ==
                 SQLITE_OK);
  InitializeSqlite(db);
  db_.reset(db);
}

SqliteEngine::~SqliteEngine() {
  // It is important to unregister any functions that have been registered with
  // the database before destroying it. This is because functions can hold onto
  // prepared statements, which must be finalized before database destruction.
  for (auto it = fn_ctx_.GetIterator(); it; ++it) {
    int ret = sqlite3_create_function_v2(db_.get(), it.key().first.c_str(),
                                         it.key().second, SQLITE_UTF8, nullptr,
                                         nullptr, nullptr, nullptr, nullptr);
    if (PERFETTO_UNLIKELY(ret != SQLITE_OK)) {
      PERFETTO_FATAL("Failed to drop function: '%s'", it.key().first.c_str());
    }
  }
  fn_ctx_.Clear();
}

SqliteEngine::PreparedStatement SqliteEngine::PrepareStatement(SqlSource sql) {
  PERFETTO_TP_TRACE(metatrace::Category::QUERY_DETAILED, "QUERY_PREPARE");
  sqlite3_stmt* raw_stmt = nullptr;
  int err =
      sqlite3_prepare_v2(db_.get(), sql.sql().c_str(), -1, &raw_stmt, nullptr);
  PreparedStatement statement{ScopedStmt(raw_stmt), std::move(sql)};
  if (err != SQLITE_OK) {
    const char* errmsg = sqlite3_errmsg(db_.get());
    std::string frame =
        statement.sql_source_.AsTracebackForSqliteOffset(GetErrorOffset());
    base::Status status = base::ErrStatus("%s%s", frame.c_str(), errmsg);
    status.SetPayload("perfetto.dev/has_traceback", "true");

    statement.status_ = std::move(status);
    return statement;
  }
  if (!raw_stmt) {
    statement.status_ = base::ErrStatus("No SQL to execute");
  }
  return statement;
}

base::Status SqliteEngine::RegisterFunction(const char* name,
                                            int argc,
                                            Fn* fn,
                                            void* ctx,
                                            FnCtxDestructor* destructor,
                                            bool deterministic) {
  int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
  int ret =
      sqlite3_create_function_v2(db_.get(), name, static_cast<int>(argc), flags,
                                 ctx, fn, nullptr, nullptr, destructor);
  if (ret != SQLITE_OK) {
    return base::ErrStatus(
        "Unable to register function with name %s: %s (SQLite error code: %d)",
        name, sqlite3_errmsg(db_.get()), ret);
  }
  *fn_ctx_.Insert(std::make_pair(name, argc), ctx).first = ctx;
  return base::OkStatus();
}

base::Status SqliteEngine::RegisterAggregateFunction(
    const char* name,
    int argc,
    AggregateFnStep* step,
    AggregateFnFinal* final,
    void* ctx,
    FnCtxDestructor* destructor,
    bool deterministic) {
  int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
  int ret =
      sqlite3_create_function_v2(db_.get(), name, static_cast<int>(argc), flags,
                                 ctx, nullptr, step, final, destructor);
  if (ret != SQLITE_OK) {
    return base::ErrStatus(
        "Unable to register aggregate function with name %s: %s (SQLite error "
        "code: %d)",
        name, sqlite3_errmsg(db_.get()), ret);
  }
  return base::OkStatus();
}

base::Status SqliteEngine::RegisterWindowFunction(const char* name,
                                                  int argc,
                                                  WindowFnStep* step,
                                                  WindowFnInverse* inverse,
                                                  WindowFnValue* value,
                                                  WindowFnFinal* final,
                                                  void* ctx,
                                                  FnCtxDestructor* destructor,
                                                  bool deterministic) {
  int flags = SQLITE_UTF8 | (deterministic ? SQLITE_DETERMINISTIC : 0);
  int ret = sqlite3_create_window_function(
      db_.get(), name, static_cast<int>(argc), flags, ctx, step, final, value,
      inverse, destructor);
  if (ret != SQLITE_OK) {
    return base::ErrStatus(
        "Unable to register window function with name %s: %s (SQLite error "
        "code: %d)",
        name, sqlite3_errmsg(db_.get()), ret);
  }
  return base::OkStatus();
}

base::Status SqliteEngine::UnregisterFunction(const char* name, int argc) {
  int ret = sqlite3_create_function_v2(db_.get(), name, static_cast<int>(argc),
                                       SQLITE_UTF8, nullptr, nullptr, nullptr,
                                       nullptr, nullptr);
  if (ret != SQLITE_OK) {
    return base::ErrStatus(
        "Unable to unregister function with name %s: %s (SQLite error code: "
        "%d)",
        name, sqlite3_errmsg(db_.get()), ret);
  }
  fn_ctx_.Erase({name, argc});
  return base::OkStatus();
}

void SqliteEngine::RegisterVirtualTableModule(
    const std::string& module_name,
    const sqlite3_module* module,
    void* ctx,
    ModuleContextDestructor destructor) {
  int res = sqlite3_create_module_v2(db_.get(), module_name.c_str(), module,
                                     ctx, destructor);
  PERFETTO_CHECK(res == SQLITE_OK);
}

void* SqliteEngine::GetFunctionContext(const std::string& name, int argc) {
  auto* res = fn_ctx_.Find(std::make_pair(name, argc));
  return res ? *res : nullptr;
}

std::optional<uint32_t> SqliteEngine::GetErrorOffset() const {
  return GetErrorOffsetDb(db_.get());
}

void* SqliteEngine::SetCommitCallback(CommitCallback callback, void* ctx) {
  return sqlite3_commit_hook(db_.get(), callback, ctx);
}

void* SqliteEngine::SetRollbackCallback(RollbackCallback callback, void* ctx) {
  return sqlite3_rollback_hook(db_.get(), callback, ctx);
}

SqliteEngine::PreparedStatement::PreparedStatement(ScopedStmt stmt,
                                                   SqlSource source)
    : stmt_(std::move(stmt)),
      expanded_sql_(sqlite3_expanded_sql(stmt_.get())),
      sql_source_(std::move(source)) {}

bool SqliteEngine::PreparedStatement::Step() {
  PERFETTO_TP_TRACE(metatrace::Category::QUERY_DETAILED, "STMT_STEP",
                    [this](metatrace::Record* record) {
                      record->AddArg("Original SQL", original_sql());
                      record->AddArg("Executed SQL", sql());
                    });

  // Now step once into |cur_stmt| so that when we prepare the next statment
  // we will have executed any dependent bytecode in this one.
  int err = sqlite3_step(stmt_.get());
  if (err == SQLITE_ROW) {
    return true;
  }
  if (err == SQLITE_DONE) {
    return false;
  }
  sqlite3* db = sqlite3_db_handle(stmt_.get());
  std::string frame =
      sql_source_.AsTracebackForSqliteOffset(GetErrorOffsetDb(db));
  const char* errmsg = sqlite3_errmsg(db);
  status_ = base::ErrStatus("%s%s", frame.c_str(), errmsg);
  return false;
}

bool SqliteEngine::PreparedStatement::IsDone() const {
  return !sqlite3_stmt_busy(stmt_.get());
}

const char* SqliteEngine::PreparedStatement::original_sql() const {
  return sql_source_.original_sql().c_str();
}

const char* SqliteEngine::PreparedStatement::sql() const {
  return expanded_sql_.get();
}

}  // namespace perfetto::trace_processor
