/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/create_function.h"

#include <queue>
#include <stack>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/parser/function_util.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/util/sql_argument.h"

namespace perfetto::trace_processor {

base::Status CreateFunction::Run(PerfettoSqlEngine* engine,
                                 size_t argc,
                                 sqlite3_value** argv,
                                 SqlValue&,
                                 Destructors&) {
  RETURN_IF_ERROR(sqlite::utils::CheckArgCount("CREATE_FUNCTION", argc, 3u));

  sqlite3_value* prototype_value = argv[0];
  sqlite3_value* return_type_value = argv[1];
  sqlite3_value* sql_defn_value = argv[2];

  // Type check all the arguments.
  {
    auto type_check = [prototype_value](sqlite3_value* value,
                                        SqlValue::Type type, const char* desc) {
      base::Status status = sqlite::utils::TypeCheckSqliteValue(value, type);
      if (!status.ok()) {
        return base::ErrStatus("CREATE_FUNCTION[prototype=%s]: %s %s",
                               sqlite3_value_text(prototype_value), desc,
                               status.c_message());
      }
      return base::OkStatus();
    };

    RETURN_IF_ERROR(type_check(prototype_value, SqlValue::Type::kString,
                               "function prototype (first argument)"));
    RETURN_IF_ERROR(type_check(return_type_value, SqlValue::Type::kString,
                               "return type (second argument)"));
    RETURN_IF_ERROR(type_check(sql_defn_value, SqlValue::Type::kString,
                               "SQL definition (third argument)"));
  }

  // Extract the arguments from the value wrappers.
  auto extract_string = [](sqlite3_value* value) -> base::StringView {
    return reinterpret_cast<const char*>(sqlite3_value_text(value));
  };
  std::string prototype_str = extract_string(prototype_value).ToStdString();
  std::string return_type_str = extract_string(return_type_value).ToStdString();
  std::string sql_defn_str = extract_string(sql_defn_value).ToStdString();

  FunctionPrototype prototype;
  RETURN_IF_ERROR(ParsePrototype(base::StringView(prototype_str), prototype));
  auto type = sql_argument::ParseType(base::StringView(return_type_str));
  if (!type) {
    return base::ErrStatus("CREATE_FUNCTION: unknown return type %s",
                           return_type_str.c_str());
  }
  return engine->RegisterRuntimeFunction(
      true /* replace */, prototype, *type,
      SqlSource::FromTraceProcessorImplementation(std::move(sql_defn_str)));
}

base::Status ExperimentalMemoize::Run(PerfettoSqlEngine* engine,
                                      size_t argc,
                                      sqlite3_value** argv,
                                      SqlValue&,
                                      Destructors&) {
  RETURN_IF_ERROR(
      sqlite::utils::CheckArgCount("EXPERIMENTAL_MEMOIZE", argc, 1));
  base::StatusOr<std::string> function_name =
      sqlite::utils::ExtractStringArg("MEMOIZE", "function_name", argv[0]);
  RETURN_IF_ERROR(function_name.status());
  return engine->EnableSqlFunctionMemoization(*function_name);
}

}  // namespace perfetto::trace_processor
