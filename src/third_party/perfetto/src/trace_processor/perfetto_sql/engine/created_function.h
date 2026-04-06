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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_CREATED_FUNCTION_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_CREATED_FUNCTION_H_

#include <sqlite3.h>
#include <cstddef>
#include <memory>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/perfetto_sql/parser/function_util.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/types/destructible.h"
#include "src/trace_processor/util/sql_argument.h"

namespace perfetto::trace_processor {

class PerfettoSqlEngine;

struct CreatedFunction : public sqlite::Function<CreatedFunction> {
  using UserData = Destructible;

  static constexpr char* kName = nullptr;
  static constexpr int kArgCount = -1;

  // sqlite::Function implementation
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);

  // Glue code for PerfettoSqlEngine.
  static std::unique_ptr<UserData> MakeContext(PerfettoSqlEngine*);
  static bool IsValid(UserData*);
  static void Reset(UserData*, PerfettoSqlEngine*);
  static base::Status Prepare(UserData*,
                              FunctionPrototype,
                              sql_argument::Type return_type,
                              SqlSource sql);
  static base::Status EnableMemoization(UserData*);
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_ENGINE_CREATED_FUNCTION_H_
