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

#include "src/trace_processor/perfetto_sql/intrinsics/operators/slice_mipmap_operator.h"

#include <sqlite3.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/implicit_segment_forest.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/module_state_manager.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {
namespace {

constexpr char kSliceSchema[] = R"(
  CREATE TABLE x(
    in_window_start BIGINT HIDDEN,
    in_window_end BIGINT HIDDEN,
    in_window_step BIGINT HIDDEN,
    ts BIGINT,
    id BIGINT,
    count INTEGER,
    dur BIGINT,
    depth INTEGER,
    PRIMARY KEY(id)
  ) WITHOUT ROWID
)";

enum ColumnIndex : size_t {
  kInWindowStart = 0,
  kInWindowEnd,
  kInWindowStep,

  kTs,
  kId,
  kCount,
  kDur,
  kDepth,
};

constexpr size_t kArgCount = kInWindowStep + 1;

bool IsArgColumn(size_t index) {
  return index < kArgCount;
}

}  // namespace

int SliceMipmapOperator::Create(sqlite3* db,
                                void* raw_ctx,
                                int argc,
                                const char* const* argv,
                                sqlite3_vtab** vtab,
                                char** zErr) {
  if (argc != 4) {
    *zErr = sqlite3_mprintf("slice_mipmap: wrong number of arguments");
    return SQLITE_ERROR;
  }

  if (int ret = sqlite3_declare_vtab(db, kSliceSchema); ret != SQLITE_OK) {
    return ret;
  }

  auto* ctx = GetContext(raw_ctx);
  auto state = std::make_unique<State>();

  std::string sql = "SELECT * FROM ";
  sql.append(argv[3]);
  auto res = ctx->engine->ExecuteUntilLastStatement(
      SqlSource::FromTraceProcessorImplementation(std::move(sql)));
  if (!res.ok()) {
    *zErr = sqlite3_mprintf("%s", res.status().c_message());
    return SQLITE_ERROR;
  }
  do {
    int64_t rawId = sqlite3_column_int64(res->stmt.sqlite_stmt(), 0);
    uint32_t id = static_cast<uint32_t>(rawId);
    if (PERFETTO_UNLIKELY(rawId != id)) {
      *zErr = sqlite3_mprintf(
          "slice_mipmap: id %lld is too large to fit in 32 bits", rawId);
      return SQLITE_ERROR;
    }
    int64_t ts = sqlite3_column_int64(res->stmt.sqlite_stmt(), 1);
    int64_t dur = sqlite3_column_int64(res->stmt.sqlite_stmt(), 2);
    auto depth =
        static_cast<uint32_t>(sqlite3_column_int64(res->stmt.sqlite_stmt(), 3));
    if (PERFETTO_UNLIKELY(depth >= state->by_depth.size())) {
      state->by_depth.resize(depth + 1);
    }
    auto& by_depth = state->by_depth[depth];
    by_depth.forest.Push(
        Slice{dur, 1, static_cast<uint32_t>(by_depth.forest.size())});
    by_depth.timestamps.push_back(ts);
    by_depth.ids.push_back(id);
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

int SliceMipmapOperator::Destroy(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  sqlite::ModuleStateManager<SliceMipmapOperator>::OnDestroy(tab->state);
  return SQLITE_OK;
}

int SliceMipmapOperator::Connect(sqlite3* db,
                                 void* raw_ctx,
                                 int argc,
                                 const char* const* argv,
                                 sqlite3_vtab** vtab,
                                 char**) {
  PERFETTO_CHECK(argc == 4);
  if (int ret = sqlite3_declare_vtab(db, kSliceSchema); ret != SQLITE_OK) {
    return ret;
  }
  auto* ctx = GetContext(raw_ctx);
  std::unique_ptr<Vtab> res = std::make_unique<Vtab>();
  res->state = ctx->OnConnect(argc, argv);
  *vtab = res.release();
  return SQLITE_OK;
}

int SliceMipmapOperator::Disconnect(sqlite3_vtab* vtab) {
  std::unique_ptr<Vtab> tab(GetVtab(vtab));
  return SQLITE_OK;
}

int SliceMipmapOperator::BestIndex(sqlite3_vtab*, sqlite3_index_info* info) {
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

int SliceMipmapOperator::Open(sqlite3_vtab*, sqlite3_vtab_cursor** cursor) {
  std::unique_ptr<Cursor> c = std::make_unique<Cursor>();
  *cursor = c.release();
  return SQLITE_OK;
}

int SliceMipmapOperator::Close(sqlite3_vtab_cursor* cursor) {
  std::unique_ptr<Cursor> c(GetCursor(cursor));
  return SQLITE_OK;
}

int SliceMipmapOperator::Filter(sqlite3_vtab_cursor* cursor,
                                int,
                                const char*,
                                int argc,
                                sqlite3_value** argv) {
  auto* c = GetCursor(cursor);
  auto* t = GetVtab(c->pVtab);
  auto* state =
      sqlite::ModuleStateManager<SliceMipmapOperator>::GetState(t->state);
  PERFETTO_CHECK(argc == kArgCount);

  c->results.clear();
  c->index = 0;

  int64_t start = sqlite3_value_int64(argv[0]);
  int64_t end = sqlite3_value_int64(argv[1]);
  int64_t step = sqlite3_value_int64(argv[2]);

  for (uint32_t depth = 0; depth < state->by_depth.size(); ++depth) {
    auto& by_depth = state->by_depth[depth];
    const auto& ids = by_depth.ids;
    const auto& tses = by_depth.timestamps;

    // If the slice before this window overlaps with the current window, move
    // the iterator back one to consider it as well.
    auto start_idx = static_cast<uint32_t>(std::distance(
        tses.begin(), std::lower_bound(tses.begin(), tses.end(), start)));
    if (start_idx != 0 &&
        (static_cast<size_t>(start_idx) == tses.size() ||
         (tses[start_idx] != start &&
          tses[start_idx] + by_depth.forest[start_idx].dur > start))) {
      --start_idx;
    }

    for (int64_t s = start; s < end; s += step) {
      auto end_idx = static_cast<uint32_t>(std::distance(
          tses.begin(),
          std::lower_bound(tses.begin() + static_cast<int64_t>(start_idx),
                           tses.end(), s + step)));
      if (start_idx == end_idx) {
        continue;
      }
      auto res = by_depth.forest.Query(start_idx, end_idx);
      c->results.emplace_back(Cursor::Result{
          tses[res.idx],
          res.dur,
          res.count,
          ids[res.idx],
          depth,
      });
      start_idx = end_idx;
    }
  }
  return SQLITE_OK;
}

int SliceMipmapOperator::Next(sqlite3_vtab_cursor* cursor) {
  GetCursor(cursor)->index++;
  return SQLITE_OK;
}

int SliceMipmapOperator::Eof(sqlite3_vtab_cursor* cursor) {
  auto* c = GetCursor(cursor);
  return c->index >= c->results.size();
}

int SliceMipmapOperator::Column(sqlite3_vtab_cursor* cursor,
                                sqlite3_context* ctx,
                                int N) {
  auto* t = GetVtab(cursor->pVtab);
  auto* c = GetCursor(cursor);
  switch (N) {
    case ColumnIndex::kTs:
      sqlite::result::Long(ctx, c->results[c->index].timestamp);
      return SQLITE_OK;
    case ColumnIndex::kId:
      sqlite::result::Long(ctx, c->results[c->index].id);
      return SQLITE_OK;
    case ColumnIndex::kCount:
      sqlite::result::Long(ctx, c->results[c->index].count);
      return SQLITE_OK;
    case ColumnIndex::kDur:
      sqlite::result::Long(ctx, c->results[c->index].dur);
      return SQLITE_OK;
    case ColumnIndex::kDepth:
      sqlite::result::Long(ctx, c->results[c->index].depth);
      return SQLITE_OK;
    default:
      return sqlite::utils::SetError(t, "Bad column");
  }
  PERFETTO_FATAL("For GCC");
}

int SliceMipmapOperator::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

}  // namespace perfetto::trace_processor
