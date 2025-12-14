/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/trace_processor/sqlite/sql_stats_table.h"

#include <sqlite3.h>
#include <memory>

#include "perfetto/base/logging.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

int SqlStatsModule::Connect(sqlite3* db,
                            void* aux,
                            int,
                            const char* const*,
                            sqlite3_vtab** vtab,
                            char**) {
  static constexpr char kSchema[] = R"(
    CREATE TABLE x(
      query TEXT,
      started BIGINT,
      first_next BIGINT,
      ended BIGINT,
      PRIMARY KEY(started)
    ) WITHOUT ROWID
  )";
  if (int ret = sqlite3_declare_vtab(db, kSchema); ret != SQLITE_OK) {
    return ret;
  }
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->storage = GetContext(aux);
  *vtab = res.release();
  return SQLITE_OK;
}

int SqlStatsModule::Disconnect(sqlite3_vtab* vtab) {
  delete GetVtab(vtab);
  return SQLITE_OK;
}

int SqlStatsModule::BestIndex(sqlite3_vtab*, sqlite3_index_info*) {
  return SQLITE_OK;
}

int SqlStatsModule::Open(sqlite3_vtab* raw_vtab, sqlite3_vtab_cursor** cursor) {
  std::unique_ptr<Cursor> c = std::make_unique<Cursor>();
  c->storage = GetVtab(raw_vtab)->storage;
  *cursor = c.release();
  return SQLITE_OK;
}

int SqlStatsModule::Close(sqlite3_vtab_cursor* cursor) {
  delete GetCursor(cursor);
  return SQLITE_OK;
}

int SqlStatsModule::Filter(sqlite3_vtab_cursor* cursor,
                           int,
                           const char*,
                           int,
                           sqlite3_value**) {
  auto* c = GetCursor(cursor);
  c->row = 0;
  c->num_rows = c->storage->sql_stats().size();
  return SQLITE_OK;
}

int SqlStatsModule::Next(sqlite3_vtab_cursor* cursor) {
  GetCursor(cursor)->row++;
  return SQLITE_OK;
}

int SqlStatsModule::Eof(sqlite3_vtab_cursor* cursor) {
  auto* c = GetCursor(cursor);
  return c->row >= c->num_rows;
}

int SqlStatsModule::Column(sqlite3_vtab_cursor* cursor,
                           sqlite3_context* ctx,
                           int N) {
  auto* c = GetCursor(cursor);
  const TraceStorage::SqlStats& stats = c->storage->sql_stats();
  switch (N) {
    case Column::kQuery:
      sqlite::result::StaticString(ctx, stats.queries()[c->row].c_str());
      break;
    case Column::kTimeStarted:
      sqlite::result::Long(ctx, stats.times_started()[c->row]);
      break;
    case Column::kTimeFirstNext:
      sqlite::result::Long(ctx, stats.times_first_next()[c->row]);
      break;
    case Column::kTimeEnded:
      sqlite::result::Long(ctx, stats.times_ended()[c->row]);
      break;
    default:
      PERFETTO_FATAL("Unknown column %d", N);
      break;
  }
  return SQLITE_OK;
}

int SqlStatsModule::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor
