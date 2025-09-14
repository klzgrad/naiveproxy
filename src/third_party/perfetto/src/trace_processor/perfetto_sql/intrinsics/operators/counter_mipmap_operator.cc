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

#include "src/trace_processor/perfetto_sql/intrinsics/operators/counter_mipmap_operator.h"

#include <sqlite3.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/containers/implicit_segment_forest.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/module_state_manager.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {
namespace {

constexpr char kSchema[] = R"(
  CREATE TABLE x(
    in_window_start BIGINT HIDDEN,
    in_window_end BIGINT HIDDEN,
    in_window_step BIGINT HIDDEN,
    min_value DOUBLE,
    max_value DOUBLE,
    last_ts BIGINT,
    last_value DOUBLE,
    PRIMARY KEY(last_ts)
  ) WITHOUT ROWID
)";

enum ColumnIndex : size_t {
  kInWindowStart = 0,
  kInWindowEnd,
  kInWindowStep,

  kMinValue,
  kMaxValue,
  kLastTs,
  kLastValue,
};

constexpr size_t kArgCount = kInWindowStep + 1;

bool IsArgColumn(size_t index) {
  return index < kArgCount;
}

using Counter = CounterMipmapOperator::Counter;
using Agg = CounterMipmapOperator::Agg;
using Forest = ImplicitSegmentForest<Counter, Agg>;

}  // namespace

int CounterMipmapOperator::Create(sqlite3* db,
                                  void* raw_ctx,
                                  int argc,
                                  const char* const* argv,
                                  sqlite3_vtab** vtab,
                                  char** zErr) {
  if (argc != 4) {
    *zErr = sqlite3_mprintf("counter_mipmap: wrong number of arguments");
    return SQLITE_ERROR;
  }

  if (int ret = sqlite3_declare_vtab(db, kSchema); ret != SQLITE_OK) {
    return ret;
  }

  auto* ctx = GetContext(raw_ctx);
  auto state = std::make_unique<State>();

  std::string sql = "SELECT ts, value FROM ";
  sql.append(argv[3]);
  auto res = ctx->engine->ExecuteUntilLastStatement(
      SqlSource::FromTraceProcessorImplementation(std::move(sql)));
  if (!res.ok()) {
    *zErr = sqlite3_mprintf("%s", res.status().c_message());
    return SQLITE_ERROR;
  }
  do {
    int64_t ts = sqlite3_column_int64(res->stmt.sqlite_stmt(), 0);
    auto value = sqlite3_column_double(res->stmt.sqlite_stmt(), 1);
    state->timestamps.push_back(ts);
    state->forest.Push(Counter{value, value});
  } while (res->stmt.Step());
  if (!res->stmt.status().ok()) {
    *zErr = sqlite3_mprintf("%s", res->stmt.status().c_message());
    return SQLITE_ERROR;
  }

  std::unique_ptr<Vtab> vtab_res = std::make_unique<Vtab>();
  vtab_res->state = ctx->OnCreate(argc, argv, std::move(state));
  *vtab = vtab_res.release();
  return SQLITE_OK;
}

int CounterMipmapOperator::Destroy(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  sqlite::ModuleStateManager<CounterMipmapOperator>::OnDestroy(tab->state);
  return SQLITE_OK;
}

int CounterMipmapOperator::Connect(sqlite3* db,
                                   void* raw_ctx,
                                   int argc,
                                   const char* const* argv,
                                   sqlite3_vtab** vtab,
                                   char**) {
  PERFETTO_CHECK(argc == 4);
  if (int ret = sqlite3_declare_vtab(db, kSchema); ret != SQLITE_OK) {
    return ret;
  }
  auto* ctx = GetContext(raw_ctx);
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->state = ctx->OnConnect(argc, argv);
  *vtab = res.release();
  return SQLITE_OK;
}

int CounterMipmapOperator::Disconnect(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  return SQLITE_OK;
}

int CounterMipmapOperator::BestIndex(sqlite3_vtab*, sqlite3_index_info* info) {
  base::Status status =
      sqlite::utils::ValidateFunctionArguments(info, kArgCount, IsArgColumn);
  if (!status.ok()) {
    return SQLITE_CONSTRAINT;
  }
  if (info->nConstraint != kArgCount) {
    return SQLITE_CONSTRAINT;
  }
  return SQLITE_OK;
}

int CounterMipmapOperator::Open(sqlite3_vtab*, sqlite3_vtab_cursor** cursor) {
  std::unique_ptr<Cursor> c = std::make_unique<Cursor>();
  *cursor = c.release();
  return SQLITE_OK;
}

int CounterMipmapOperator::Close(sqlite3_vtab_cursor* cursor) {
  std::unique_ptr<Cursor> c(GetCursor(cursor));
  return SQLITE_OK;
}

int CounterMipmapOperator::Filter(sqlite3_vtab_cursor* cursor,
                                  int,
                                  const char*,
                                  int argc,
                                  sqlite3_value** argv) {
  auto* c = GetCursor(cursor);
  auto* t = GetVtab(c->pVtab);
  auto* state =
      sqlite::ModuleStateManager<CounterMipmapOperator>::GetState(t->state);
  PERFETTO_CHECK(argc == kArgCount);

  int64_t start_ts = sqlite3_value_int64(argv[0]);
  int64_t end_ts = sqlite3_value_int64(argv[1]);
  int64_t step_ts = sqlite3_value_int64(argv[2]);

  c->index = 0;
  c->counters.clear();

  // If there is a counter value before the start of this window, include it in
  // the aggregation as well because it contributes to what should be rendered
  // here.
  auto ts_lb = std::lower_bound(state->timestamps.begin(),
                                state->timestamps.end(), start_ts);
  if (ts_lb != state->timestamps.begin() &&
      (ts_lb == state->timestamps.end() || *ts_lb != start_ts)) {
    --ts_lb;
  }
  int64_t start_idx = std::distance(state->timestamps.begin(), ts_lb);
  for (int64_t s = start_ts; s < end_ts; s += step_ts) {
    int64_t end_idx =
        std::distance(state->timestamps.begin(),
                      std::lower_bound(state->timestamps.begin() +
                                           static_cast<int64_t>(start_idx),
                                       state->timestamps.end(), s + step_ts));
    if (start_idx == end_idx) {
      continue;
    }
    c->counters.emplace_back(Cursor::Result{
        state->forest.Query(static_cast<uint32_t>(start_idx),
                            static_cast<uint32_t>(end_idx)),
        state->forest[static_cast<uint32_t>(end_idx) - 1],
        state->timestamps[static_cast<uint32_t>(end_idx) - 1],
    });
    start_idx = end_idx;
  }
  return SQLITE_OK;
}

int CounterMipmapOperator::Next(sqlite3_vtab_cursor* cursor) {
  GetCursor(cursor)->index++;
  return SQLITE_OK;
}

int CounterMipmapOperator::Eof(sqlite3_vtab_cursor* cursor) {
  auto* c = GetCursor(cursor);
  return c->index >= c->counters.size();
}

int CounterMipmapOperator::Column(sqlite3_vtab_cursor* cursor,
                                  sqlite3_context* ctx,
                                  int N) {
  auto* t = GetVtab(cursor->pVtab);
  auto* c = GetCursor(cursor);
  const auto& res = c->counters[c->index];
  switch (N) {
    case ColumnIndex::kMinValue:
      sqlite::result::Double(ctx, res.min_max_counter.min);
      return SQLITE_OK;
    case ColumnIndex::kMaxValue:
      sqlite::result::Double(ctx, res.min_max_counter.max);
      return SQLITE_OK;
    case ColumnIndex::kLastTs:
      sqlite::result::Long(ctx, res.last_ts);
      return SQLITE_OK;
    case ColumnIndex::kLastValue:
      PERFETTO_DCHECK(
          std::equal_to<>()(res.last_counter.min, res.last_counter.max));
      sqlite::result::Double(ctx, res.last_counter.min);
      return SQLITE_OK;
    default:
      return sqlite::utils::SetError(t, "Bad column");
  }
  PERFETTO_FATAL("For GCC");
}

int CounterMipmapOperator::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor
