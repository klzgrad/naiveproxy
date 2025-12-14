/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/trace_processor/sqlite/sqlite_utils.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/sqlite/scoped_db.h"

namespace perfetto::trace_processor::sqlite::utils {
namespace internal {
namespace {
std::string ToExpectedTypesString(ExpectedTypesSet expected_types) {
  PERFETTO_CHECK(expected_types.any());
  std::stringstream ss;
  if (expected_types.count() > 1) {
    ss << "any of ";
  }

  bool add_separator = false;
  for (size_t i = 0; i < expected_types.size(); ++i) {
    if (expected_types[i]) {
      ss << (add_separator ? ", " : "")
         << SqliteTypeToFriendlyString(static_cast<SqlValue::Type>(i));
      add_separator = true;
    }
  }

  return ss.str();
}

inline SqlValue::Type SqliteTypeToSqlValueType(int sqlite_type) {
  switch (sqlite_type) {
    case SQLITE_NULL:
      return SqlValue::Type::kNull;
    case SQLITE_BLOB:
      return SqlValue::Type::kBytes;
    case SQLITE_INTEGER:
      return SqlValue::Type::kLong;
    case SQLITE_FLOAT:
      return SqlValue::Type::kDouble;
    case SQLITE_TEXT:
      return SqlValue::Type::kString;
  }
  PERFETTO_FATAL("Unknown SQLite type %d", sqlite_type);
}

}  // namespace

base::Status InvalidArgumentTypeError(const char* argument_name,
                                      size_t arg_index,
                                      SqlValue::Type actual_type,
                                      ExpectedTypesSet expected_types) {
  return ToInvalidArgumentError(
      argument_name, arg_index,
      base::ErrStatus("does not have expected type. Expected %s but found %s",
                      ToExpectedTypesString(expected_types).c_str(),
                      SqliteTypeToFriendlyString(actual_type)));
}

base::StatusOr<SqlValue> ExtractArgument(size_t argc,
                                         sqlite3_value** argv,
                                         const char* argument_name,
                                         size_t arg_index,
                                         ExpectedTypesSet expected_types) {
  if (arg_index >= argc) {
    return MissingArgumentError(argument_name);
  }

  SqlValue value = sqlite::utils::SqliteValueToSqlValue(argv[arg_index]);
  if (!expected_types.test(value.type)) {
    return InvalidArgumentTypeError(argument_name, arg_index, value.type,
                                    expected_types);
  }
  return value;
}
}  // namespace internal

base::Status GetColumnsForTable(
    sqlite3* db,
    const std::string& raw_table_name,
    std::vector<std::pair<SqlValue::Type, std::string>>& columns) {
  PERFETTO_DCHECK(columns.empty());
  char sql[1024];
  const char kRawSql[] = "SELECT name, type from pragma_table_info(\"%s\")";

  // Support names which are table valued functions with arguments.
  std::string table_name = raw_table_name.substr(0, raw_table_name.find('('));
  size_t n = base::SprintfTrunc(sql, sizeof(sql), kRawSql, table_name.c_str());
  PERFETTO_DCHECK(n > 0);

  sqlite3_stmt* raw_stmt = nullptr;
  int err =
      sqlite3_prepare_v2(db, sql, static_cast<int>(n), &raw_stmt, nullptr);
  if (err != SQLITE_OK) {
    return base::ErrStatus("Preparing database failed");
  }
  ScopedStmt stmt(raw_stmt);
  PERFETTO_DCHECK(sqlite3_column_count(*stmt) == 2);

  for (;;) {
    err = sqlite3_step(raw_stmt);
    if (err == SQLITE_DONE)
      break;
    if (err != SQLITE_ROW) {
      return base::ErrStatus("Querying schema of table %s failed",
                             raw_table_name.c_str());
    }

    const char* name =
        reinterpret_cast<const char*>(sqlite3_column_text(*stmt, 0));
    const char* raw_type =
        reinterpret_cast<const char*>(sqlite3_column_text(*stmt, 1));
    if (!name || !raw_type || !*name) {
      return base::ErrStatus("Schema for %s has invalid column values",
                             raw_table_name.c_str());
    }

    SqlValue::Type type;
    if (base::CaseInsensitiveEqual(raw_type, "STRING") ||
        base::CaseInsensitiveEqual(raw_type, "TEXT")) {
      type = SqlValue::Type::kString;
    } else if (base::CaseInsensitiveEqual(raw_type, "DOUBLE")) {
      type = SqlValue::Type::kDouble;
    } else if (base::CaseInsensitiveEqual(raw_type, "BIG INT") ||
               base::CaseInsensitiveEqual(raw_type, "BIGINT") ||
               base::CaseInsensitiveEqual(raw_type, "UNSIGNED INT") ||
               base::CaseInsensitiveEqual(raw_type, "INT") ||
               base::CaseInsensitiveEqual(raw_type, "BOOLEAN") ||
               base::CaseInsensitiveEqual(raw_type, "INTEGER")) {
      type = SqlValue::Type::kLong;
    } else if (base::CaseInsensitiveEqual(raw_type, "BLOB")) {
      type = SqlValue::Type::kBytes;
    } else if (!*raw_type) {
      PERFETTO_DLOG("Unknown column type for %s %s", raw_table_name.c_str(),
                    name);
      type = SqlValue::Type::kNull;
    } else {
      return base::ErrStatus("Unknown column type '%s' on table %s", raw_type,
                             raw_table_name.c_str());
    }
    columns.emplace_back(type, name);
  }

  // Catch mis-spelt table names.
  //
  // A SELECT on pragma_table_info() returns no rows if the
  // table that was queried is not present.
  if (columns.empty()) {
    return base::ErrStatus("Unknown table or view name '%s'",
                           raw_table_name.c_str());
  }

  return base::OkStatus();
}

const char* SqliteTypeToFriendlyString(SqlValue::Type type) {
  switch (type) {
    case SqlValue::Type::kNull:
      return "NULL";
    case SqlValue::Type::kLong:
      return "BOOL/INT/UINT/LONG";
    case SqlValue::Type::kDouble:
      return "FLOAT/DOUBLE";
    case SqlValue::Type::kString:
      return "STRING";
    case SqlValue::Type::kBytes:
      return "BYTES/PROTO";
  }
  PERFETTO_FATAL("For GCC");
}

base::Status CheckArgCount(const char* function_name,
                           size_t argc,
                           size_t expected_argc) {
  if (argc == expected_argc) {
    return base::OkStatus();
  }
  return base::ErrStatus("%s: expected %zu arguments, got %zu", function_name,
                         expected_argc, argc);
}

base::StatusOr<std::string> ExtractStringArg(const char* function_name,
                                             const char* arg_name,
                                             sqlite3_value* sql_value) {
  SqlValue value = SqliteValueToSqlValue(sql_value);
  if (value.type != SqlValue::kString) {
    return base::ErrStatus(
        "%s(%s): value has type %s which does not match the expected type %s",
        function_name, arg_name, SqliteTypeToFriendlyString(value.type),
        SqliteTypeToFriendlyString(SqlValue::kString));
  }
  const char* result = value.AsString();
  PERFETTO_CHECK(result);
  return std::string(result);
}

base::Status TypeCheckSqliteValue(sqlite3_value* value,
                                  SqlValue::Type expected_type) {
  return TypeCheckSqliteValue(value, expected_type,
                              SqliteTypeToFriendlyString(expected_type));
}

base::Status TypeCheckSqliteValue(sqlite3_value* value,
                                  SqlValue::Type expected_type,
                                  const char* expected_type_str) {
  SqlValue::Type actual_type =
      internal::SqliteTypeToSqlValueType(sqlite3_value_type(value));
  if (actual_type != SqlValue::Type::kNull && actual_type != expected_type) {
    return base::ErrStatus(
        "does not have expected type: expected %s, actual %s",
        expected_type_str, SqliteTypeToFriendlyString(actual_type));
  }
  return base::OkStatus();
}

base::Status MissingArgumentError(const char* argument_name) {
  return base::ErrStatus("argument missing: %s", argument_name);
}

base::Status ToInvalidArgumentError(const char* argument_name,
                                    size_t arg_index,
                                    const base::Status& error) {
  return base::ErrStatus("argument %s at pos %zu: %s", argument_name,
                         arg_index + 1, error.message().c_str());
}

}  // namespace perfetto::trace_processor::sqlite::utils
