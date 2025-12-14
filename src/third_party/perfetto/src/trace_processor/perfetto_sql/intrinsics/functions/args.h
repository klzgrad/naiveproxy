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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_ARGS_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_ARGS_H_

#include "perfetto/base/status.h"
#include "perfetto/ext/base/dynamic_string_writer.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/util/args_utils.h"

namespace perfetto::trace_processor {

// extract_arg(arg_set_id, arg_name) returns the value of the given argument
// from a given arg set.
struct ExtractArg : public sqlite::Function<ExtractArg> {
  static constexpr char kName[] = "extract_arg";
  static constexpr int kArgCount = 2;

  using UserData = TraceStorage;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv);
};

// Prints the entire arg set as a json object.
struct ArgSetToJson : public sqlite::Function<ArgSetToJson> {
  static constexpr char kName[] = "__intrinsic_arg_set_to_json";
  static constexpr int kArgCount = 1;

  struct Context {
    explicit Context(TraceStorage* s)
        : storage(s),
          arg_cursor(
              s->mutable_arg_table()->CreateCursor({dataframe::FilterSpec{
                  tables::ArgTable::ColumnIndex::arg_set_id,
                  0,
                  dataframe::Eq{},
                  std::nullopt,
              }})) {}

    TraceStorage* storage;
    tables::ArgTable::Cursor arg_cursor;
    base::DynamicStringWriter json_writer;
    ArgSet arg_set;
  };

  using UserData = Context;
  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv);
};
}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_ARGS_H_
