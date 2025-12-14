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

#include "src/trace_processor/sqlite/stats_table.h"

#include <sqlite3.h>
#include <memory>

#include "perfetto/base/logging.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

int StatsModule::Connect(sqlite3* db,
                         void* aux,
                         int,
                         const char* const*,
                         sqlite3_vtab** vtab,
                         char**) {
  static constexpr char kSchema[] = R"(
    CREATE TABLE x(
      name TEXT,
      idx BIGINT,
      severity TEXT,
      source TEXT,
      value BIGINT,
      description TEXT,
      PRIMARY KEY(name)
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

int StatsModule::Disconnect(sqlite3_vtab* vtab) {
  delete GetVtab(vtab);
  return SQLITE_OK;
}

int StatsModule::BestIndex(sqlite3_vtab*, sqlite3_index_info*) {
  return SQLITE_OK;
}

int StatsModule::Open(sqlite3_vtab* raw_vtab, sqlite3_vtab_cursor** cursor) {
  std::unique_ptr<Cursor> c = std::make_unique<Cursor>();
  c->storage = GetVtab(raw_vtab)->storage;
  *cursor = c.release();
  return SQLITE_OK;
}

int StatsModule::Close(sqlite3_vtab_cursor* cursor) {
  delete GetCursor(cursor);
  return SQLITE_OK;
}

int StatsModule::Filter(sqlite3_vtab_cursor* cursor,
                        int,
                        const char*,
                        int,
                        sqlite3_value**) {
  auto* c = GetCursor(cursor);
  c->key = {};
  c->it = {};
  return SQLITE_OK;
}

int StatsModule::Next(sqlite3_vtab_cursor* cursor) {
  static_assert(stats::kTypes[0] == stats::kSingle,
                "the first stats entry cannot be indexed");

  auto* c = GetCursor(cursor);
  const auto* cur_entry = &c->storage->stats()[c->key];
  if (stats::kTypes[c->key] == stats::kIndexed) {
    if (++c->it != cur_entry->indexed_values.end()) {
      return SQLITE_OK;
    }
  }
  while (++c->key < stats::kNumKeys) {
    cur_entry = &c->storage->stats()[c->key];
    c->it = cur_entry->indexed_values.begin();
    if (stats::kTypes[c->key] == stats::kSingle ||
        !cur_entry->indexed_values.empty()) {
      break;
    }
  }
  return SQLITE_OK;
}

int StatsModule::Eof(sqlite3_vtab_cursor* cursor) {
  return GetCursor(cursor)->key >= stats::kNumKeys;
}

int StatsModule::Column(sqlite3_vtab_cursor* cursor,
                        sqlite3_context* ctx,
                        int N) {
  auto* c = GetCursor(cursor);
  switch (N) {
    case Column::kName:
      sqlite::result::StaticString(ctx, stats::kNames[c->key]);
      break;
    case Column::kIndex:
      if (stats::kTypes[c->key] == stats::kIndexed) {
        sqlite::result::Long(ctx, c->it->first);
      } else {
        sqlite::result::Null(ctx);
      }
      break;
    case Column::kSeverity:
      switch (stats::kSeverities[c->key]) {
        case stats::kInfo:
          sqlite::result::StaticString(ctx, "info");
          break;
        case stats::kDataLoss:
          sqlite::result::StaticString(ctx, "data_loss");
          break;
        case stats::kError:
          sqlite::result::StaticString(ctx, "error");
          break;
      }
      break;
    case Column::kSource:
      switch (stats::kSources[c->key]) {
        case stats::kTrace:
          sqlite::result::StaticString(ctx, "trace");
          break;
        case stats::kAnalysis:
          sqlite::result::StaticString(ctx, "analysis");
          break;
      }
      break;
    case Column::kValue:
      if (stats::kTypes[c->key] == stats::kIndexed) {
        sqlite::result::Long(ctx, c->it->second);
      } else {
        sqlite::result::Long(ctx, c->storage->stats()[c->key].value);
      }
      break;
    case Column::kDescription:
      sqlite::result::StaticString(ctx, stats::kDescriptions[c->key]);
      break;
    default:
      PERFETTO_FATAL("Unknown column %d", N);
      break;
  }
  return SQLITE_OK;
}

int StatsModule::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor
