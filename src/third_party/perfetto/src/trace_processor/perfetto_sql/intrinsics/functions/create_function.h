/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_CREATE_FUNCTION_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_CREATE_FUNCTION_H_

#include <cstddef>

#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"

namespace perfetto::trace_processor {

class PerfettoSqlEngine;

// Implementation of CREATE_FUNCTION SQL function.
// See https://perfetto.dev/docs/analysis/metrics#metric-helper-functions for
// usage of this function.
struct CreateFunction : public sqlite::Function<CreateFunction> {
  static constexpr char kName[] = "create_function";
  static constexpr int kArgCount = 3;

  using UserData = PerfettoSqlEngine;
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

// Implementation of MEMOIZE SQL function.
// SELECT EXPERIMENTAL_MEMOIZE('my_func') enables memoization for the results of
// the calls to `my_func`. `my_func` must be a Perfetto SQL function created
// through CREATE_FUNCTION that takes a single integer argument and returns a
// int.
struct ExperimentalMemoize : public sqlite::Function<ExperimentalMemoize> {
  static constexpr char kName[] = "experimental_memoize";
  static constexpr int kArgCount = 1;

  using UserData = PerfettoSqlEngine;
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_CREATE_FUNCTION_H_
