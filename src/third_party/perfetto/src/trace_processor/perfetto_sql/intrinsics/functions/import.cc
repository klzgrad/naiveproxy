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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/import.h"

#include <cstddef>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

void Import::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  auto* engine = GetUserData(ctx);

  if (sqlite::value::Type(argv[0]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(ctx, "IMPORT: argument must be string");
  }

  const char* import_key = sqlite::value::Text(argv[0]);
  base::StackString<1024> create("INCLUDE PERFETTO MODULE %s;", import_key);
  auto status = engine
                    ->Execute(SqlSource::FromTraceProcessorImplementation(
                        create.ToStdString()))
                    .status();
  if (!status.ok()) {
    return sqlite::utils::SetError(ctx, status);
  }

  // IMPORT returns no value (void function)
  return sqlite::utils::ReturnVoidFromFunction(ctx);
}

}  // namespace perfetto::trace_processor
