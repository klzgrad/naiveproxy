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

base::Status CreateViewFunction::Run(CreateViewFunction::Context* ctx,
                                     size_t argc,
                                     sqlite3_value** argv,
                                     SqlValue&,
                                     Destructors&) {
  if (argc != 3) {
    return base::ErrStatus(
        "CREATE_VIEW_FUNCTION: invalid number of args; expected %u, received "
        "%zu",
        3u, argc);
  }

  sqlite3_value* prototype_value = argv[0];
  sqlite3_value* return_prototype_value = argv[1];
  sqlite3_value* sql_defn_value = argv[2];

  // Type check all the arguments.
  {
    auto type_check = [prototype_value](sqlite3_value* value,
                                        SqlValue::Type type, const char* desc) {
      base::Status status = sqlite::utils::TypeCheckSqliteValue(value, type);
      if (!status.ok()) {
        return base::ErrStatus("CREATE_VIEW_FUNCTION[prototype=%s]: %s %s",
                               sqlite3_value_text(prototype_value), desc,
                               status.c_message());
      }
      return base::OkStatus();
    };

    RETURN_IF_ERROR(type_check(prototype_value, SqlValue::Type::kString,
                               "function prototype (first argument)"));
    RETURN_IF_ERROR(type_check(return_prototype_value, SqlValue::Type::kString,
                               "return prototype (second argument)"));
    RETURN_IF_ERROR(type_check(sql_defn_value, SqlValue::Type::kString,
                               "SQL definition (third argument)"));
  }

  // Extract the arguments from the value wrappers.
  auto extract_string = [](sqlite3_value* value) -> const char* {
    return reinterpret_cast<const char*>(sqlite3_value_text(value));
  };

  const char* prototype_str = extract_string(prototype_value);
  const char* return_prototype_str = extract_string(return_prototype_value);
  const char* sql_defn_str = extract_string(sql_defn_value);

  static constexpr char kSqlTemplate[] =
      R"""(CREATE OR REPLACE PERFETTO FUNCTION %s RETURNS TABLE(%s) AS %s;)""";

  base::StringView function_name;
  RETURN_IF_ERROR(ParseFunctionName(prototype_str, function_name));

  std::string function_name_str = function_name.ToStdString();

  NullTermStringView sql_defn(sql_defn_str);
  std::string formatted_sql(sql_defn.size() + 1024, '\0');
  size_t size = base::SprintfTrunc(formatted_sql.data(), formatted_sql.size(),
                                   kSqlTemplate, prototype_str,
                                   return_prototype_str, sql_defn_str);
  formatted_sql.resize(size);

  auto res = ctx->Execute(
      SqlSource::FromTraceProcessorImplementation(std::move(formatted_sql)));
  return res.status();
}

}  // namespace perfetto::trace_processor
