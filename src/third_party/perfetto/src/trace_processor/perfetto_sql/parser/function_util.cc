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

#include "src/trace_processor/perfetto_sql/parser/function_util.h"

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

std::string FunctionPrototype::ToString() const {
  return function_name + "(" + SerializeArguments(arguments) + ")";
}

base::Status ParseFunctionName(base::StringView raw, base::StringView& out) {
  size_t function_name_end = raw.find('(');
  if (function_name_end == base::StringView::npos)
    return base::ErrStatus("unable to find bracket starting argument list");

  base::StringView function_name = raw.substr(0, function_name_end);
  if (!sql_argument::IsValidName(function_name)) {
    return base::ErrStatus("function name %s is not alphanumeric",
                           function_name.ToStdString().c_str());
  }
  out = function_name;
  return base::OkStatus();
}

base::Status ParsePrototype(base::StringView raw, FunctionPrototype& out) {
  // Examples of function prototypes:
  // ANDROID_SDK_LEVEL()
  // STARTUP_SLICE(dur_ns INT)
  // FIND_NEXT_SLICE_WITH_NAME(ts INT, name STRING)

  base::StringView function_name;
  RETURN_IF_ERROR(ParseFunctionName(raw, function_name));

  size_t function_name_end = function_name.size();
  size_t args_start = function_name_end + 1;
  size_t args_end = raw.find(')', args_start);
  if (args_end == base::StringView::npos)
    return base::ErrStatus("unable to find bracket ending argument list");

  base::StringView args_str = raw.substr(args_start, args_end - args_start);
  RETURN_IF_ERROR(sql_argument::ParseArgumentDefinitions(args_str.ToStdString(),
                                                         out.arguments));

  out.function_name = function_name.ToStdString();
  return base::OkStatus();
}

base::Status SqliteRetToStatus(sqlite3* db,
                               const std::string& function_name,
                               int ret) {
  if (ret != SQLITE_ROW && ret != SQLITE_DONE) {
    return base::ErrStatus("%s: SQLite error while executing function body: %s",
                           function_name.c_str(), sqlite3_errmsg(db));
  }
  return base::OkStatus();
}

base::Status MaybeBindArgument(sqlite3_stmt* stmt,
                               const std::string& function_name,
                               const sql_argument::ArgumentDefinition& arg,
                               sqlite3_value* value) {
  int index = sqlite3_bind_parameter_index(stmt, arg.dollar_name().c_str());

  // If the argument is not in the query, this just means its an unused
  // argument which we can just ignore.
  if (index == 0)
    return base::Status();

  int ret = sqlite3_bind_value(stmt, index, value);
  if (ret != SQLITE_OK) {
    return base::ErrStatus(
        "%s: SQLite error while binding value to argument %s: %s",
        function_name.c_str(), arg.name().c_str(),
        sqlite3_errmsg(sqlite3_db_handle(stmt)));
  }
  return base::OkStatus();
}

base::Status MaybeBindIntArgument(sqlite3_stmt* stmt,
                                  const std::string& function_name,
                                  const sql_argument::ArgumentDefinition& arg,
                                  int64_t value) {
  int index = sqlite3_bind_parameter_index(stmt, arg.dollar_name().c_str());

  // If the argument is not in the query, this just means its an unused
  // argument which we can just ignore.
  if (index == 0)
    return base::Status();

  int ret = sqlite3_bind_int64(stmt, index, value);
  if (ret != SQLITE_OK) {
    return base::ErrStatus(
        "%s: SQLite error while binding value to argument %s: %s",
        function_name.c_str(), arg.name().c_str(),
        sqlite3_errmsg(sqlite3_db_handle(stmt)));
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
