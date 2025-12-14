/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_SQLITE_SQLITE_UTILS_H_
#define SRC_TRACE_PROCESSOR_SQLITE_SQLITE_UTILS_H_

#include <sqlite3.h>
#include <bitset>
#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"  // IWYU pragma: keep
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"

// Analogous to ASSIGN_OR_RETURN macro. Returns an sqlite error.
#define SQLITE_RETURN_IF_ERROR(vtab, expr)                                  \
  do {                                                                      \
    const base::Status& status_macro_internal_status = (expr);              \
    if (!status_macro_internal_status.ok())                                 \
      return sqlite::utils::SetError((vtab), status_macro_internal_status); \
  } while (0)

// Analogous to ASSIGN_OR_RETURN macro. Returns an sqlite error.
#define SQLITE_ASSIGN_OR_RETURN(vtab, lhs, rhs)                            \
  PERFETTO_INTERNAL_MACRO_CONCAT(auto status_or, __LINE__) = rhs;          \
  SQLITE_RETURN_IF_ERROR(                                                  \
      vtab, PERFETTO_INTERNAL_MACRO_CONCAT(status_or, __LINE__).status()); \
  lhs = std::move(PERFETTO_INTERNAL_MACRO_CONCAT(status_or, __LINE__).value())

namespace perfetto::trace_processor::sqlite::utils {

const auto kSqliteStatic = reinterpret_cast<sqlite3_destructor_type>(0);
const auto kSqliteTransient = reinterpret_cast<sqlite3_destructor_type>(-1);

inline bool IsOpEq(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_EQ;
}
inline bool IsOpLe(int op) {
  return op == SQLITE_INDEX_CONSTRAINT_LE;
}

inline SqlValue SqliteValueToSqlValue(sqlite3_value* value) {
  SqlValue sql_value;
  switch (sqlite3_value_type(value)) {
    case SQLITE_INTEGER:
      sql_value.type = SqlValue::Type::kLong;
      sql_value.long_value = sqlite3_value_int64(value);
      break;
    case SQLITE_FLOAT:
      sql_value.type = SqlValue::Type::kDouble;
      sql_value.double_value = sqlite3_value_double(value);
      break;
    case SQLITE_TEXT:
      sql_value.type = SqlValue::Type::kString;
      sql_value.string_value =
          reinterpret_cast<const char*>(sqlite3_value_text(value));
      break;
    case SQLITE_BLOB:
      sql_value.type = SqlValue::Type::kBytes;
      sql_value.bytes_value = sqlite3_value_blob(value);
      sql_value.bytes_count = static_cast<size_t>(sqlite3_value_bytes(value));
      break;
  }
  return sql_value;
}

inline std::optional<std::string> SqlValueToString(SqlValue value) {
  switch (value.type) {
    case SqlValue::Type::kString:
      return value.AsString();
    case SqlValue::Type::kDouble:
      return std::to_string(value.AsDouble());
    case SqlValue::Type::kLong:
      return std::to_string(value.AsLong());
    case SqlValue::Type::kBytes:
    case SqlValue::Type::kNull:
      return std::nullopt;
  }
  PERFETTO_FATAL("For GCC");
}

inline int SetError(sqlite3_vtab* tab, const char* status) {
  sqlite3_free(tab->zErrMsg);
  tab->zErrMsg = sqlite3_mprintf("%s", status);
  return SQLITE_ERROR;
}

inline void SetError(sqlite3_context* ctx, const char* status) {
  sqlite::result::Error(ctx, status);
}

inline int SetError(sqlite3_vtab* tab, base::Status s) {
  return SetError(tab, s.c_message());
}

inline void SetError(sqlite3_context* ctx, const base::Status& status) {
  PERFETTO_CHECK(!status.ok());
  sqlite::result::Error(ctx, status.c_message());
}

inline void SetError(sqlite3_context* ctx,
                     const std::string& function_name,
                     const base::Status& status) {
  SetError(ctx, base::ErrStatus("%s: %s", function_name.c_str(),
                                status.c_message()));
}

// Return NULL from a SQLite function implementation. This is more efficient
// than calling sqlite::result::Null(ctx) because SQLite functions automatically
// return NULL by default when no result is set.
// IMPORTANT: Only use this inside SQLite function Step() implementations.
inline void ReturnNullFromFunction(sqlite3_context*) {
  // Intentionally empty - SQLite functions return NULL by default
}

// Return from a void SQLite function implementation. This sets a special "VOID"
// pointer type to prevent the function result from being included in query
// output. Use this for functions that perform side effects but don't return
// values. IMPORTANT: Only use this inside SQLite function Step()
// implementations.
inline void ReturnVoidFromFunction(sqlite3_context* ctx) {
  // Set the "VOID" pointer type to a non-null value. Note that because of the
  // weird way |sqlite3_value_pointer| works, we need to set some value even if
  // we don't actually read it - just set it to a pointer to an empty string for
  // this reason.
  static char kVoidValue[] = "";
  sqlite::result::StaticPointer(ctx, kVoidValue, "VOID");
}

// For a given |sqlite3_index_info| struct received in a BestIndex call, returns
// whether all |arg_count| arguments (with |is_arg_column| indicating whether a
// given column is a function argument) have exactly one equality constraint
// associated with them.
//
// If so, the associated constraint is omitted and the argvIndex is mapped to
// the corresponding argument's index.
inline base::Status ValidateFunctionArguments(
    sqlite3_index_info* info,
    size_t arg_count,
    const std::function<bool(size_t)>& is_arg_column) {
  std::vector<bool> present;
  size_t present_count = 0;
  for (int i = 0; i < info->nConstraint; ++i) {
    const auto& in = info->aConstraint[i];
    if (!in.usable) {
      continue;
    }
    auto cs_col = static_cast<size_t>(in.iColumn);
    if (!is_arg_column(cs_col)) {
      continue;
    }
    if (!IsOpEq(in.op)) {
      return base::ErrStatus(
          "Unexpected non equality constraints for column %zu", cs_col);
    }
    if (cs_col >= present.size()) {
      present.resize(cs_col + 1);
    }
    if (present[cs_col]) {
      return base::ErrStatus("Unexpected multiple constraints for column %zu",
                             cs_col);
    }
    present[cs_col] = true;
    present_count++;

    auto& out = info->aConstraintUsage[i];
    out.argvIndex = static_cast<int>(present_count);
    out.omit = true;
  }
  if (present_count != arg_count) {
    return base::ErrStatus(
        "Unexpected missing argument: expected %zu, actual %zu", arg_count,
        present_count);
  }
  return base::OkStatus();
}

// Converts the given SqlValue type to the type string SQLite understands.
inline std::string SqlValueTypeToSqliteTypeName(SqlValue::Type type) {
  switch (type) {
    case SqlValue::Type::kString:
      return "TEXT";
    case SqlValue::Type::kLong:
      return "BIGINT";
    case SqlValue::Type::kDouble:
      return "DOUBLE";
    case SqlValue::Type::kBytes:
      return "BLOB";
    case SqlValue::Type::kNull:
      // Default to BIGINT for columns which contains only NULLs - if we don't
      // specify the type, sqlite will default to BLOB, which is going to trip
      // a number of various checks.
      return "BIGINT";
  }
  PERFETTO_FATAL("Not reached");  // For gcc
}

// Returns the column names for the table named by |raw_table_name|.
base::Status GetColumnsForTable(
    sqlite3* db,
    const std::string& raw_table_name,
    std::vector<std::pair<SqlValue::Type, std::string>>& columns);

// Given an SqlValue::Type, converts it to a human-readable string.
// This should really only be used for debugging messages.
const char* SqliteTypeToFriendlyString(SqlValue::Type type);

// Verifies if |argc| matches |expected_argc| and returns an appropriate error
// message if they don't match.
base::Status CheckArgCount(const char* function_name,
                           size_t argc,
                           size_t expected_argc);

// Type-safe helpers to extract an arg value from a sqlite3_value*, returning an
// appropriate message if it fails.
base::StatusOr<std::string> ExtractStringArg(const char* function_name,
                                             const char* arg_name,
                                             sqlite3_value* value);

// Verifies if |value| has the type represented by |expected_type|.
// Returns base::OkStatus if it does or an base::ErrStatus with an
// appropriate error message (incorporating |expected_type_str| if specified).
base::Status TypeCheckSqliteValue(sqlite3_value* value,
                                  SqlValue::Type expected_type);
base::Status TypeCheckSqliteValue(sqlite3_value* value,
                                  SqlValue::Type expected_type,
                                  const char* expected_type_str);

namespace internal {

static_assert(sizeof(size_t) * 8 > SqlValue::kLastType);
using ExpectedTypesSet = std::bitset<SqlValue::kLastType + 1>;

template <typename... args>
constexpr ExpectedTypesSet ToExpectedTypesSet(args... expected_type_args) {
  ExpectedTypesSet set;
  for (const SqlValue::Type t : {expected_type_args...}) {
    set.set(static_cast<size_t>(t));
  }
  return set;
}

base::StatusOr<SqlValue> ExtractArgument(size_t argc,
                                         sqlite3_value** argv,
                                         const char* argument_name,
                                         size_t arg_index,
                                         ExpectedTypesSet expected_types);
base::Status InvalidArgumentTypeError(const char* argument_name,
                                      size_t arg_index,
                                      SqlValue::Type actual_type,
                                      ExpectedTypesSet expected_types);
}  // namespace internal

template <typename... args>
base::Status InvalidArgumentTypeError(const char* argument_name,
                                      size_t arg_index,
                                      SqlValue::Type actual_type,
                                      SqlValue::Type expected_type,
                                      args... expected_type_args) {
  return internal::InvalidArgumentTypeError(
      argument_name, arg_index, actual_type,
      internal::ToExpectedTypesSet(expected_type, expected_type_args...));
}

base::Status MissingArgumentError(const char* argument_name);

base::Status ToInvalidArgumentError(const char* argument_name,
                                    size_t arg_index,
                                    const base::Status& error);

template <typename... args>
base::StatusOr<SqlValue> ExtractArgument(size_t argc,
                                         sqlite3_value** argv,
                                         const char* argument_name,
                                         size_t arg_index,
                                         SqlValue::Type expected_type,
                                         args... expected_type_args) {
  return internal::ExtractArgument(
      argc, argv, argument_name, arg_index,
      internal::ToExpectedTypesSet(expected_type, expected_type_args...));
}

}  // namespace perfetto::trace_processor::sqlite::utils

#endif  // SRC_TRACE_PROCESSOR_SQLITE_SQLITE_UTILS_H_
