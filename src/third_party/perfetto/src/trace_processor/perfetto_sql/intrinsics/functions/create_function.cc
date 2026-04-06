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

void CreateFunction::Step(sqlite3_context* ctx,
                          int argc,
                          sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 3);

  auto* engine = GetUserData(ctx);

  // Type check all the arguments.
  if (sqlite::value::Type(argv[0]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(
        ctx,
        "CREATE_FUNCTION: function prototype (first argument) must be string");
  }
  if (sqlite::value::Type(argv[1]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(
        ctx, "CREATE_FUNCTION: return type (second argument) must be string");
  }
  if (sqlite::value::Type(argv[2]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(
        ctx, "CREATE_FUNCTION: SQL definition (third argument) must be string");
  }

  // Extract the arguments from the value wrappers.
  std::string prototype_str = sqlite::value::Text(argv[0]);
  std::string return_type_str = sqlite::value::Text(argv[1]);
  std::string sql_defn_str = sqlite::value::Text(argv[2]);

  FunctionPrototype prototype;
  auto parse_status =
      ParsePrototype(base::StringView(prototype_str), prototype);
  if (!parse_status.ok()) {
    return sqlite::utils::SetError(ctx, parse_status);
  }

  auto type = sql_argument::ParseType(base::StringView(return_type_str));
  if (!type) {
    return sqlite::utils::SetError(
        ctx, base::ErrStatus("CREATE_FUNCTION: unknown return type %s",
                             return_type_str.c_str()));
  }

  auto register_status = engine->RegisterLegacyRuntimeFunction(
      true /* replace */, prototype, *type,
      SqlSource::FromTraceProcessorImplementation(std::move(sql_defn_str)));
  if (!register_status.ok()) {
    return sqlite::utils::SetError(ctx, register_status);
  }

  // CREATE_FUNCTION returns no value (void function)
  return sqlite::utils::ReturnVoidFromFunction(ctx);
}

void ExperimentalMemoize::Step(sqlite3_context* ctx,
                               int argc,
                               sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  auto* engine = GetUserData(ctx);

  if (sqlite::value::Type(argv[0]) != sqlite::Type::kText) {
    return sqlite::utils::SetError(
        ctx, "EXPERIMENTAL_MEMOIZE: function_name must be string");
  }

  std::string function_name = sqlite::value::Text(argv[0]);
  auto status = engine->EnableSqlFunctionMemoization(function_name);
  if (!status.ok()) {
    return sqlite::utils::SetError(ctx, status);
  }

  // EXPERIMENTAL_MEMOIZE returns no value (void function)
  return sqlite::utils::ReturnVoidFromFunction(ctx);
}

}  // namespace perfetto::trace_processor
