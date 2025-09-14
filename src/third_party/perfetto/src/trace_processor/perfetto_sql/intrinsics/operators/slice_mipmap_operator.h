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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_OPERATORS_SLICE_MIPMAP_OPERATOR_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_OPERATORS_SLICE_MIPMAP_OPERATOR_H_

#include <sqlite3.h>
#include <cstdint>
#include <vector>

#include "src/trace_processor/containers/implicit_segment_forest.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/bindings/sqlite_module.h"
#include "src/trace_processor/sqlite/module_state_manager.h"

namespace perfetto::trace_processor {

// Operator for building "mipmaps"  [1] over the slices in the trace.
//
// In this context mipmap really means aggregating the slices in a given
// time period by max(dur) for that period, allowing UIs to efficiently display
// the contents of slice tracks when very zoomed out.
//
// Specifically, we are computing the query:
// ```
//   select
//     depth,
//     max(dur) as dur,
//     id,
//     ts
//   from $input in
//   where in.ts_end >= $window_start and in.ts <= $window_end
//   group by depth, ts / $window_resolution
//   order by ts
// ```
// but in O(logn) time by using a segment-tree like data structure (see
// ImplicitSegmentForest).
//
// [1] https://en.wikipedia.org/wiki/Mipmap
struct SliceMipmapOperator : sqlite::Module<SliceMipmapOperator> {
  struct Slice {
    int64_t dur;
    uint32_t count;
    uint32_t idx;
  };
  struct Agg {
    Slice operator()(const Slice& a, const Slice& b) {
      return a.dur < b.dur ? Slice{b.dur, a.count + b.count, b.idx}
                           : Slice{a.dur, a.count + b.count, a.idx};
    }
  };
  struct PerDepth {
    ImplicitSegmentForest<Slice, Agg> forest;
    std::vector<uint32_t> ids;
    std::vector<int64_t> timestamps;
  };
  struct State {
    std::vector<PerDepth> by_depth;
  };
  struct Context : sqlite::ModuleStateManager<SliceMipmapOperator> {
    explicit Context(PerfettoSqlEngine* _engine) : engine(_engine) {}
    PerfettoSqlEngine* engine;
  };
  struct Vtab : sqlite::Module<SliceMipmapOperator>::Vtab {
    sqlite::ModuleStateManager<SliceMipmapOperator>::PerVtabState* state;
  };
  struct Cursor : sqlite::Module<SliceMipmapOperator>::Cursor {
    struct Result {
      int64_t timestamp;
      int64_t dur;
      uint32_t count;
      uint32_t id;
      uint32_t depth;
    };
    std::vector<Result> results;
    uint32_t index = 0;
  };

  static constexpr auto kType = kCreateOnly;
  static constexpr bool kSupportsWrites = false;
  static constexpr bool kDoesOverloadFunctions = false;

  static int Create(sqlite3*,
                    void*,
                    int,
                    const char* const*,
                    sqlite3_vtab**,
                    char**);
  static int Destroy(sqlite3_vtab*);

  static int Connect(sqlite3*,
                     void*,
                     int,
                     const char* const*,
                     sqlite3_vtab**,
                     char**);
  static int Disconnect(sqlite3_vtab*);

  static int BestIndex(sqlite3_vtab*, sqlite3_index_info*);

  static int Open(sqlite3_vtab*, sqlite3_vtab_cursor**);
  static int Close(sqlite3_vtab_cursor*);

  static int Filter(sqlite3_vtab_cursor*,
                    int,
                    const char*,
                    int,
                    sqlite3_value**);
  static int Next(sqlite3_vtab_cursor*);
  static int Eof(sqlite3_vtab_cursor*);
  static int Column(sqlite3_vtab_cursor*, sqlite3_context*, int);
  static int Rowid(sqlite3_vtab_cursor*, sqlite_int64*);

  static int Begin(sqlite3_vtab*) { return SQLITE_OK; }
  static int Sync(sqlite3_vtab*) { return SQLITE_OK; }
  static int Commit(sqlite3_vtab*) { return SQLITE_OK; }
  static int Rollback(sqlite3_vtab*) { return SQLITE_OK; }
  static int Savepoint(sqlite3_vtab* t, int r) {
    SliceMipmapOperator::Vtab* vtab = GetVtab(t);
    sqlite::ModuleStateManager<SliceMipmapOperator>::OnSavepoint(vtab->state,
                                                                 r);
    return SQLITE_OK;
  }
  static int Release(sqlite3_vtab* t, int r) {
    SliceMipmapOperator::Vtab* vtab = GetVtab(t);
    sqlite::ModuleStateManager<SliceMipmapOperator>::OnRelease(vtab->state, r);
    return SQLITE_OK;
  }
  static int RollbackTo(sqlite3_vtab* t, int r) {
    SliceMipmapOperator::Vtab* vtab = GetVtab(t);
    sqlite::ModuleStateManager<SliceMipmapOperator>::OnRollbackTo(vtab->state,
                                                                  r);
    return SQLITE_OK;
  }

  // This needs to happen at the end as it depends on the functions
  // defined above.
  static constexpr sqlite3_module kModule = CreateModule();
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_OPERATORS_SLICE_MIPMAP_OPERATOR_H_
