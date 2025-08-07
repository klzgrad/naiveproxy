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

base::Status Import::Run(Import::Context* ctx,
                         size_t argc,
                         sqlite3_value** argv,
                         SqlValue&,
                         Destructors&) {
  if (argc != 1) {
    return base::ErrStatus(
        "IMPORT: invalid number of args; expected 1, received "
        "%zu",
        argc);
  }
  sqlite3_value* import_val = argv[0];

  // Type check
  {
    base::Status status = sqlite::utils::TypeCheckSqliteValue(
        import_val, SqlValue::Type::kString);
    if (!status.ok()) {
      return base::ErrStatus("IMPORT(%s): %s", sqlite3_value_text(import_val),
                             status.c_message());
    }
  }

  const char* import_key =
      reinterpret_cast<const char*>(sqlite3_value_text(import_val));
  base::StackString<1024> create("INCLUDE PERFETTO MODULE %s;", import_key);
  return ctx->engine
      ->Execute(
          SqlSource::FromTraceProcessorImplementation(create.ToStdString()))
      .status();
}

}  // namespace perfetto::trace_processor
