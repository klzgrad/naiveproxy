// Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_PACKAGE_LOOKUP_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_PACKAGE_LOOKUP_H_

#include <optional>

#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"

namespace perfetto::trace_processor {

// package_lookup(uid) returns an approprioate display name for a given uid.
struct PackageLookup : public sqlite::Function<PackageLookup> {
  static constexpr char kName[] = "package_lookup";
  static constexpr int kArgCount = 1;

  struct Context {
    Context(TraceStorage* s)
        : storage(s),
          // TODO(rzuklie): the uid column is not indexed (since it is not
          // unique), consider finding a more efficient way to scan the table.
          package_list_cursor(s->mutable_package_list_table()->CreateCursor(
              {dataframe::FilterSpec{
                  tables::PackageListTable::ColumnIndex::uid,
                  0,
                  dataframe::Eq{},
                  std::nullopt,
              }})) {}

    TraceStorage* storage;
    tables::PackageListTable::Cursor package_list_cursor;
  };

  using UserData = Context;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv);
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_PACKAGE_LOOKUP_H_
