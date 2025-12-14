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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/create_view_function.h"
#include <cstddef>
#include <string>
#include <utility>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/parser/function_util.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

void CreateViewFunction::Step(sqlite3_context* ctx,
                              int argc,
                              sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 3);

  auto* engine = GetUserData(ctx);

  // Type check all the arguments.
  if (sqlite::value::Type(argv[0]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(ctx,
                                   "CREATE_VIEW_FUNCTION: function prototype "
                                   "(first argument) must be string");
  }
  if (sqlite::value::Type(argv[1]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(ctx,
                                   "CREATE_VIEW_FUNCTION: return prototype "
                                   "(second argument) must be string");
  }
  if (sqlite::value::Type(argv[2]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(
        ctx,
        "CREATE_VIEW_FUNCTION: SQL definition (third argument) must be string");
  }

  // Extract the arguments from the value wrappers.
  const char* prototype_str = sqlite::value::Text(argv[0]);
  const char* return_prototype_str = sqlite::value::Text(argv[1]);
  const char* sql_defn_str = sqlite::value::Text(argv[2]);

  static constexpr char kSqlTemplate[] =
      R"""(CREATE OR REPLACE PERFETTO FUNCTION %s RETURNS TABLE(%s) AS %s;)""";

  base::StringView function_name;
  auto parse_status = ParseFunctionName(prototype_str, function_name);
  if (!parse_status.ok()) {
    return sqlite::utils::SetError(ctx, parse_status);
  }

  std::string function_name_str = function_name.ToStdString();

  NullTermStringView sql_defn(sql_defn_str);
  std::string formatted_sql(sql_defn.size() + 1024, '\0');
  size_t size = base::SprintfTrunc(formatted_sql.data(), formatted_sql.size(),
                                   kSqlTemplate, prototype_str,
                                   return_prototype_str, sql_defn_str);
  formatted_sql.resize(size);

  auto res = engine->Execute(
      SqlSource::FromTraceProcessorImplementation(std::move(formatted_sql)));
  if (!res.status().ok()) {
    return sqlite::utils::SetError(ctx, res.status());
  }

  // CREATE_VIEW_FUNCTION returns no value (void function)
  return sqlite::utils::ReturnVoidFromFunction(ctx);
}

}  // namespace perfetto::trace_processor
