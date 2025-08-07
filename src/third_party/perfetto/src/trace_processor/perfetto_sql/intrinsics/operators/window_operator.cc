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

#include "src/trace_processor/perfetto_sql/intrinsics/operators/window_operator.h"

#include <sqlite3.h>
#include <cstdint>
#include <memory>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

namespace {
constexpr char kSchema[] = R"(
    CREATE TABLE x(
      rowid BIGINT HIDDEN,
      quantum BIGINT HIDDEN,
      window_start BIGINT HIDDEN,
      window_dur BIGINT HIDDEN,
      ts BIGINT,
      dur BIGINT,
      quantum_ts BIGINT,
      PRIMARY KEY(rowid)
    ) WITHOUT ROWID
  )";
}

enum Column {
  kRowId = 0,
  kQuantum = 1,
  kWindowStart = 2,
  kWindowDur = 3,
  kTs = 4,
  kDuration = 5,
  kQuantumTs = 6
};

int WindowOperatorModule::Create(sqlite3* db,
                                 void*,
                                 int argc,
                                 const char* const* argv,
                                 sqlite3_vtab** vtab,
                                 char** pzErr) {
  // The first three arguments are SQLite generated arguments which should
  // always be present.
  PERFETTO_CHECK(argc >= 3);
  if (argc != 6) {
    *pzErr = sqlite3_mprintf(
        "Expected 3 arguments to __intrinsic_window, got %d", argc - 3);
    return SQLITE_ERROR;
  }

  std::optional<int64_t> window_start = base::StringToInt64(argv[3]);
  std::optional<int64_t> window_dur = base::StringToInt64(argv[4]);
  std::optional<int64_t> quantum = base::StringToInt64(argv[5]);

  if (!window_start || !window_dur || !quantum) {
    *pzErr = sqlite3_mprintf("Unable to parse arguments as numbers: %s, %s, %s",
                             argv[3], argv[4], argv[5]);
    return SQLITE_ERROR;
  }
  if (int ret = sqlite3_declare_vtab(db, kSchema); ret != SQLITE_OK) {
    return ret;
  }
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->window_start = window_start.value();
  res->window_dur = window_dur.value();
  res->quantum = quantum.value();
  *vtab = res.release();
  return SQLITE_OK;
}

int WindowOperatorModule::Destroy(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  return SQLITE_OK;
}

int WindowOperatorModule::Connect(sqlite3* db,
                                  void* raw_ctx,
                                  int argc,
                                  const char* const* argv,
                                  sqlite3_vtab** vtab,
                                  char** pzErr) {
  return Create(db, raw_ctx, argc, argv, vtab, pzErr);
}

int WindowOperatorModule::Disconnect(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  return SQLITE_OK;
}

int WindowOperatorModule::BestIndex(sqlite3_vtab*, sqlite3_index_info* info) {
  info->orderByConsumed = info->nOrderBy == 1 &&
                          info->aOrderBy[0].iColumn == Column::kTs &&
                          !info->aOrderBy[0].desc;

  // Set return first if there is a equals constraint on the row id asking to
  // return the first row.
  bool is_row_id_constraint = info->nConstraint == 1 &&
                              info->aConstraint[0].iColumn == Column::kRowId &&
                              info->aConstraint[0].usable &&
                              sqlite::utils::IsOpEq(info->aConstraint[0].op);
  if (is_row_id_constraint) {
    info->idxNum = 1;
    info->aConstraintUsage[0].argvIndex = 1;
  } else {
    info->idxNum = 0;
  }
  return SQLITE_OK;
}

int WindowOperatorModule::Open(sqlite3_vtab*, sqlite3_vtab_cursor** cursor) {
  std::unique_ptr<Cursor> c = std::make_unique<Cursor>();
  *cursor = c.release();
  return SQLITE_OK;
}

int WindowOperatorModule::Close(sqlite3_vtab_cursor* cursor) {
  delete GetCursor(cursor);
  return SQLITE_OK;
}

int WindowOperatorModule::Filter(sqlite3_vtab_cursor* cursor,
                                 int is_row_id_constraint,
                                 const char*,
                                 int argc,
                                 sqlite3_value** argv) {
  auto* t = GetVtab(cursor->pVtab);
  auto* c = GetCursor(cursor);

  c->window_end = t->window_start + t->window_dur;
  c->step_size = t->quantum == 0 ? t->window_dur : t->quantum;
  c->current_ts = t->window_start;

  if (is_row_id_constraint) {
    PERFETTO_CHECK(argc == 1);
    c->filter_type = sqlite3_value_int(argv[0]) == 0 ? FilterType::kReturnFirst
                                                     : FilterType::kReturnAll;
  } else {
    c->filter_type = FilterType::kReturnAll;
  }
  return SQLITE_OK;
}

int WindowOperatorModule::Next(sqlite3_vtab_cursor* cursor) {
  auto* c = GetCursor(cursor);
  switch (c->filter_type) {
    case FilterType::kReturnFirst:
      c->current_ts = c->window_end;
      break;
    case FilterType::kReturnAll:
      c->current_ts += c->step_size;
      c->quantum_ts++;
      break;
  }
  c->row_id++;
  return SQLITE_OK;
}

int WindowOperatorModule::Eof(sqlite3_vtab_cursor* cursor) {
  auto* c = GetCursor(cursor);
  return c->current_ts >= c->window_end;
}

int WindowOperatorModule::Column(sqlite3_vtab_cursor* cursor,
                                 sqlite3_context* ctx,
                                 int N) {
  auto* t = GetVtab(cursor->pVtab);
  auto* c = GetCursor(cursor);
  switch (N) {
    case Column::kQuantum: {
      sqlite::result::Long(ctx, static_cast<sqlite_int64>(t->quantum));
      break;
    }
    case Column::kWindowStart: {
      sqlite::result::Long(ctx, static_cast<sqlite_int64>(t->window_start));
      break;
    }
    case Column::kWindowDur: {
      sqlite::result::Long(ctx, static_cast<int>(t->window_dur));
      break;
    }
    case Column::kTs: {
      sqlite::result::Long(ctx, static_cast<sqlite_int64>(c->current_ts));
      break;
    }
    case Column::kDuration: {
      sqlite::result::Long(ctx, static_cast<sqlite_int64>(c->step_size));
      break;
    }
    case Column::kQuantumTs: {
      sqlite::result::Long(ctx, static_cast<sqlite_int64>(c->quantum_ts));
      break;
    }
    case Column::kRowId: {
      sqlite::result::Long(ctx, static_cast<sqlite_int64>(c->row_id));
      break;
    }
    default: {
      PERFETTO_FATAL("Unknown column %d", N);
      break;
    }
  }
  return SQLITE_OK;
}

int WindowOperatorModule::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor
