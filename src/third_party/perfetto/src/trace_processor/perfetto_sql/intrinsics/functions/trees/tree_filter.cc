/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/trees/tree_filter.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/tree/tree_transformer.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

namespace {

// Converts a string operator to a dataframe::Op type.
// Returns an error for unknown operators.
base::StatusOr<core::dataframe::Op> StringOperatorToOp(
    const std::string& op_str) {
  if (op_str == "=") {
    return core::dataframe::Op(core::dataframe::Eq{});
  }
  if (op_str == "!=") {
    return core::dataframe::Op(core::dataframe::Ne{});
  }
  if (op_str == "<") {
    return core::dataframe::Op(core::dataframe::Lt{});
  }
  if (op_str == "<=") {
    return core::dataframe::Op(core::dataframe::Le{});
  }
  if (op_str == ">") {
    return core::dataframe::Op(core::dataframe::Gt{});
  }
  if (op_str == ">=") {
    return core::dataframe::Op(core::dataframe::Ge{});
  }
  if (op_str == "GLOB") {
    return core::dataframe::Op(core::dataframe::Glob{});
  }
  if (op_str == "REGEX") {
    return core::dataframe::Op(core::dataframe::Regex{});
  }
  if (op_str == "IS NULL") {
    return core::dataframe::Op(core::dataframe::IsNull{});
  }
  if (op_str == "IS NOT NULL") {
    return core::dataframe::Op(core::dataframe::IsNotNull{});
  }
  if (op_str == "IN") {
    return core::dataframe::Op(core::dataframe::In{});
  }
  return base::ErrStatus("Unknown operator: %s", op_str.c_str());
}

// Represents a single filter constraint (column, operator, value).
struct FilterConstraint {
  std::string column_name;
  std::string op_str;
  SqlValue value;
};

// Represents a list of filter constraints combined with a logical operator.
struct FilterConstraints {
  enum class LogicOp { kAnd, kOr };
  LogicOp logic_op = LogicOp::kAnd;
  std::vector<FilterConstraint> constraints;
};

}  // namespace

void TreeConstraint::Step(sqlite3_context* ctx,
                          int argc,
                          sqlite3_value** argv) {
  if (argc != 3) {
    return sqlite::result::Error(
        ctx, "tree_constraint: expected 3 arguments (column, op, value)");
  }

  // Extract column name.
  SQLITE_ASSIGN_OR_RETURN(ctx, auto column_name,
                          sqlite::utils::ExtractArgument(
                              static_cast<uint32_t>(argc), argv, "column name",
                              0, SqlValue::Type::kString));

  // Extract operator.
  SQLITE_ASSIGN_OR_RETURN(
      ctx, auto op,
      sqlite::utils::ExtractArgument(static_cast<uint32_t>(argc), argv, "op", 1,
                                     SqlValue::Type::kString));

  // Extract value (any type).
  auto value = sqlite::utils::SqliteValueToSqlValue(argv[2]);

  // Create and return a FilterConstraint pointer.
  auto constraint = std::make_unique<FilterConstraint>();
  constraint->column_name = column_name.AsString();
  constraint->op_str = op.AsString();
  constraint->value = value;

  return sqlite::result::UniquePointer(ctx, std::move(constraint),
                                       "FILTER_CONSTRAINT");
}

void TreeWhereAnd::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  auto constraints = std::make_unique<FilterConstraints>();
  constraints->logic_op = FilterConstraints::LogicOp::kAnd;

  // Collect all non-NULL constraint pointers.
  // Note: SQLite pointer values have type() == kNull, so we must try to
  // extract the pointer first before checking if the value is NULL.
  for (int i = 0; i < argc; ++i) {
    auto* constraint_ptr =
        sqlite::value::Pointer<FilterConstraint>(argv[i], "FILTER_CONSTRAINT");
    if (constraint_ptr) {
      constraints->constraints.push_back(*constraint_ptr);
      continue;
    }
    // Not a valid pointer - check if it's actually NULL (allowed)
    if (sqlite::value::Type(argv[i]) == sqlite::Type::kNull) {
      continue;
    }
    // Not a pointer and not NULL - that's an error
    return sqlite::result::Error(
        ctx, "tree_where_and: expected FILTER_CONSTRAINT or NULL");
  }

  // If no constraints were provided (either 0 args or all NULL), return NULL
  // which acts as a no-op filter (no filtering applied).
  if (constraints->constraints.empty()) {
    return sqlite::utils::ReturnNullFromFunction(ctx);
  }

  return sqlite::result::UniquePointer(ctx, std::move(constraints),
                                       "FILTER_CONSTRAINTS");
}

void TreeFilter::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  if (argc != 2) {
    return sqlite::result::Error(
        ctx, "tree_filter: expected 2 arguments (tree_ptr, where_clause)");
  }

  // Extract tree pointer.
  auto* tree_ptr =
      sqlite::value::Pointer<sqlite::utils::MovePointer<tree::TreeTransformer>>(
          argv[0], "TREE_TRANSFORMER");
  if (!tree_ptr) {
    return sqlite::result::Error(ctx, "tree_filter: expected TREE_TRANSFORMER");
  }
  if (tree_ptr->taken()) {
    return sqlite::result::Error(ctx,
                                 "tree_filter: tree has already been consumed");
  }

  // Extract filter constraints.
  auto* constraints_ptr =
      sqlite::value::Pointer<FilterConstraints>(argv[1], "FILTER_CONSTRAINTS");
  if (!constraints_ptr) {
    return sqlite::result::Error(ctx,
                                 "tree_filter: expected FILTER_CONSTRAINTS");
  }

  // Only AND is supported for tree filters.
  if (constraints_ptr->logic_op != FilterConstraints::LogicOp::kAnd) {
    return sqlite::result::Error(ctx, "tree_filter: OR not supported");
  }

  // Take ownership of the transformer.
  auto transformer = tree_ptr->Take();

  std::vector<core::dataframe::FilterSpec> specs;
  std::vector<SqlValue> values;
  uint32_t idx = 0;
  for (const auto& c : constraints_ptr->constraints) {
    auto col = transformer.df().IndexOfColumnLegacy(c.column_name);
    if (!col) {
      base::StackString<256> err("tree_filter: unknown column '%s'",
                                 c.column_name.c_str());
      return sqlite::result::Error(ctx, err.c_str());
    }
    SQLITE_ASSIGN_OR_RETURN(ctx, auto op, StringOperatorToOp(c.op_str));
    specs.push_back({*col, idx++, op, std::nullopt});
    values.push_back(c.value);
  }

  auto status = transformer.FilterTree(std::move(specs), std::move(values));
  SQLITE_RETURN_IF_ERROR(ctx, status);

  // Return the modified transformer wrapped in a new MovePointer.
  return sqlite::result::UniquePointer(
      ctx,
      std::make_unique<sqlite::utils::MovePointer<tree::TreeTransformer>>(
          std::move(transformer)),
      "TREE_TRANSFORMER");
}

}  // namespace perfetto::trace_processor
