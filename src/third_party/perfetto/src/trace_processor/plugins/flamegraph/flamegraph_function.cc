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

#include "src/trace_processor/plugins/flamegraph/flamegraph_function.h"

#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/regex.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/core/tree/tree.h"
#include "src/trace_processor/core/tree/tree_from_dataframe.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/perfetto_sql/engine/sqlite_dataframe_builder.h"
#include "src/trace_processor/plugins/flamegraph/flamegraph.h"
#include "src/trace_processor/sqlite/bindings/sqlite_column.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/module_state_manager.h"
#include "src/trace_processor/sqlite/sql_source.h"
#include "src/trace_processor/sqlite/sqlite_tagged_args.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {
namespace {

constexpr char kConfigPointerType[] = "FLAMEGRAPH_CONFIG";

constexpr uint32_t kConfigColumn = 0;
constexpr uint32_t kTreeIdColumn = 1;
constexpr uint32_t kTreeParentIdColumn = 2;
constexpr uint32_t kDepthColumn = 3;
constexpr uint32_t kNameColumn = 4;
constexpr uint32_t kSelfValueColumn = 5;
constexpr uint32_t kCumulativeValueColumn = 6;
constexpr uint32_t kParentCumulativeValueColumn = 7;
constexpr uint32_t kXStartColumn = 8;
constexpr uint32_t kXEndColumn = 9;
constexpr uint32_t kPropertyColumnStart = 10;
constexpr uint32_t kArgCount = 1;
constexpr int kSuperRootConstraint = SQLITE_INDEX_CONSTRAINT_FUNCTION + 1;
constexpr int kSuperRootPlan = 1;

bool IsArgumentColumn(size_t column) {
  return column == kConfigColumn;
}

base::StatusOr<FlamegraphRegexSpec> ParseRegexSpec(const char* context,
                                                   sqlite3_value* pattern,
                                                   sqlite3_value* flags) {
  ASSIGN_OR_RETURN(std::string parsed_pattern,
                   sqlite::TaggedArgText(context, pattern));
  ASSIGN_OR_RETURN(std::string parsed_flags,
                   sqlite::TaggedArgText(context, flags));
  if (!parsed_flags.empty() && parsed_flags != "i") {
    return base::ErrStatus("%s: unknown regex flags '%s'", context,
                           parsed_flags.c_str());
  }
  return FlamegraphRegexSpec{std::move(parsed_pattern), parsed_flags == "i"};
}

// Parses a text token which must be one of the named values.
template <typename T>
base::StatusOr<T> ParseToken(
    const char* context,
    sqlite3_value* value,
    std::initializer_list<std::pair<const char*, T>> tokens) {
  ASSIGN_OR_RETURN(std::string token, sqlite::TaggedArgText(context, value));
  for (const auto& [name, parsed] : tokens) {
    if (token == name) {
      return parsed;
    }
  }
  return base::ErrStatus("%s: unknown value '%s'", context, token.c_str());
}

base::StatusOr<base::Regex> CompileRegex(const FlamegraphRegexSpec& spec) {
  const auto sensitivity = spec.case_insensitive
                               ? base::Regex::CaseSensitivity::kInsensitive
                               : base::Regex::CaseSensitivity::kSensitive;
  return base::Regex::Create(spec.pattern, sensitivity);
}

base::StatusOr<const core::Tree::Column*> ResolveColumn(
    const core::Tree& tree,
    const char* context,
    const std::string& name) {
  auto column = tree.Find(name);
  if (!column) {
    return base::ErrStatus("%s: column '%s' does not exist", context,
                           name.c_str());
  }
  return *column;
}

base::StatusOr<flamegraph::Config> ResolveConfig(const core::Tree& source,
                                                 StringPool& pool,
                                                 const FlamegraphQuery& query) {
  flamegraph::Config config(pool);
  config.view = query.view;
  if (query.view_pattern) {
    ASSIGN_OR_RETURN(config.view_pattern, CompileRegex(*query.view_pattern));
  }
  ASSIGN_OR_RETURN(config.name,
                   ResolveColumn(source, "flamegraph: name", "name"));
  if (!config.name->type.Is<core::String>()) {
    return base::ErrStatus("flamegraph: name column must be a string");
  }
  for (const std::string& name : query.grouping_columns) {
    ASSIGN_OR_RETURN(
        auto column,
        ResolveColumn(source, "flamegraph: grouping column", name));
    config.grouping_columns.push_back(column);
  }
  for (const std::string& name : query.value_columns) {
    ASSIGN_OR_RETURN(auto column,
                     ResolveColumn(source, "flamegraph: value column", name));
    if (!flamegraph::IsNumericColumn(*column)) {
      return base::ErrStatus("flamegraph: value columns must be numeric");
    }
    config.value_columns.push_back(column);
  }
  if (config.value_columns.empty()) {
    return base::ErrStatus("flamegraph: at least one value column is required");
  }
  for (const FlamegraphFilterSpec& filter : query.filters) {
    ASSIGN_OR_RETURN(auto regex, CompileRegex(filter.regex));
    switch (filter.kind) {
      case FlamegraphFilterSpec::Kind::kShowStack:
        config.show_stack_filters.push_back(std::move(regex));
        break;
      case FlamegraphFilterSpec::Kind::kHideStack:
        config.hide_stack_filters.push_back(std::move(regex));
        break;
      case FlamegraphFilterSpec::Kind::kHideFrame:
        config.hide_frame_filters.push_back(std::move(regex));
        break;
    }
  }
  if (config.show_stack_filters.size() >
      flamegraph::Config::kMaxShowStackFilters) {
    return base::ErrStatus("flamegraph: too many SHOW_STACK filters");
  }
  for (const FlamegraphAggregateSpec& aggregate : query.aggregate_columns) {
    if (aggregate.output_name != aggregate.input_name) {
      return base::ErrStatus(
          "flamegraph: aggregate output must match its source column");
    }
    ASSIGN_OR_RETURN(auto column,
                     ResolveColumn(source, "flamegraph: aggregate column",
                                   aggregate.input_name));
    if (column->type.Is<core::Null>()) {
      // A column with no values aggregates to no values under any mode.
    } else if (aggregate.aggregate == flamegraph::Config::Aggregate::kSum &&
               !flamegraph::IsNumericColumn(*column)) {
      return base::ErrStatus(
          "flamegraph: SUM aggregate columns must be numeric");
    }
    config.aggregate_columns.push_back(
        {column, aggregate.aggregate, aggregate.output_name});
  }
  return std::move(config);
}

bool IsReservedOutputName(std::string_view name) {
  return name == "in_config" || name == "_tree_id" ||
         name == "_tree_parent_id" || name == "depth" || name == "name" ||
         name == "self_value" || name == "cumulative_value" ||
         name == "parent_cumulative_value" || name == "x_start" ||
         name == "x_end";
}

// Number of fixed leading source columns: id, parent_id, name, value.
constexpr uint32_t kFixedSourceColumns = 4;

base::StatusOr<std::unique_ptr<FlamegraphOperator::State>> LoadSource(
    FlamegraphOperator::Context* context,
    const char* source) {
  std::string sql = "SELECT * FROM ";
  sql.append(source);
  ASSIGN_OR_RETURN(
      auto execution,
      context->connection->ExecuteUntilLastStatement(
          SqlSource::FromTraceProcessorImplementation(std::move(sql))));
  sqlite3_stmt* stmt = execution.stmt.sqlite_stmt();
  const uint32_t column_count = sqlite::column::Count(stmt);
  if (column_count < kFixedSourceColumns) {
    return base::ErrStatus(
        "flamegraph: source must contain id, parent_id, name and value");
  }
  std::vector<std::string> names;
  names.reserve(column_count);
  for (uint32_t column = 0; column < column_count; ++column) {
    names.emplace_back(sqlite::column::Name(stmt, column));
  }
  if (names[0] != "id" || names[1] != "parent_id" || names[2] != "name" ||
      names[3] != "value") {
    return base::ErrStatus(
        "flamegraph: first source columns must be id, parent_id, name, value");
  }
  for (uint32_t column = kFixedSourceColumns; column < column_count; ++column) {
    if (IsReservedOutputName(names[column])) {
      return base::ErrStatus("flamegraph: reserved property column '%s'",
                             names[column].c_str());
    }
  }

  SqliteDataframeBuilderOptions options;
  options.nullability = dataframe::NullabilityType::kDenseNull;
  ASSIGN_OR_RETURN(auto builder, BuildRuntimeDataframeFromSqliteStatement(
                                     context->pool, names, &execution.stmt,
                                     "flamegraph", std::move(options)));
  ASSIGN_OR_RETURN(core::Tree tree, core::BuildTree(std::move(builder)));
  auto state = std::make_unique<FlamegraphOperator::State>();
  state->pool = context->pool;
  state->source = std::move(tree);
  state->property_names.assign(names.begin() + kFixedSourceColumns,
                               names.end());
  for (uint32_t column = kFixedSourceColumns; column < column_count; ++column) {
    if (flamegraph::IsNumericColumn(state->source.columns[column])) {
      state->candidate_value_names.push_back(names[column]);
    }
  }
  const core::Tree::Column* value = *state->source.Find("value");
  if (state->source.row_count > 0 && !flamegraph::IsNumericColumn(*value)) {
    return base::ErrStatus("flamegraph: value column must be numeric");
  }
  for (uint32_t row = 0; row < state->source.row_count; ++row) {
    if (value->null_bv.size() > 0 && !value->null_bv.is_set(row)) {
      continue;
    }
    state->source_value_sum +=
        value->type.Is<core::Int64>()
            ? static_cast<double>(value->unchecked_data<int64_t>()[row])
            : value->unchecked_data<double>()[row];
  }
  state->candidate_column_start =
      kPropertyColumnStart +
      static_cast<uint32_t>(state->property_names.size());
  state->output_column_count =
      state->candidate_column_start +
      2 * static_cast<uint32_t>(state->candidate_value_names.size());
  return std::move(state);
}

std::string QuoteIdentifier(std::string_view identifier) {
  std::string quoted(1, '"');
  for (char c : identifier) {
    quoted.push_back(c);
    if (c == '"') {
      quoted.push_back('"');
    }
  }
  quoted.push_back('"');
  return quoted;
}

std::string BuildSchema(const FlamegraphOperator::State& state) {
  std::string schema = R"(
    CREATE TABLE x(
      in_config BLOB HIDDEN,
      _tree_id BIGINT,
      _tree_parent_id BIGINT,
      depth BIGINT,
      name TEXT,
      self_value,
      cumulative_value,
      parent_cumulative_value,
      x_start,
      x_end)";
  for (const std::string& property : state.property_names) {
    schema.append(",\n      ");
    schema.append(QuoteIdentifier(property));
  }
  for (const std::string& value : state.candidate_value_names) {
    schema.append(",\n      ");
    schema.append(QuoteIdentifier("self_" + value));
    schema.append(",\n      ");
    schema.append(QuoteIdentifier("cumulative_" + value));
  }
  schema.append(R"(,
      PRIMARY KEY(_tree_id)
    ) WITHOUT ROWID
  )");
  return schema;
}

int DeclareSchema(sqlite3* db,
                  const FlamegraphOperator::State& state,
                  char** error) {
  std::string schema = BuildSchema(state);
  int result = sqlite3_declare_vtab(db, schema.c_str());
  if (result != SQLITE_OK && error) {
    *error = sqlite3_mprintf("flamegraph: failed to declare schema");
  }
  return result;
}

void ReturnTreeColumn(sqlite3_context* context,
                      StringPool& pool,
                      const core::Tree::Column* column,
                      uint32_t row) {
  if (!column || (column->null_bv.size() > 0 && !column->null_bv.is_set(row))) {
    sqlite::result::Null(context);
    return;
  }
  if (column->type.Is<core::Int64>()) {
    sqlite::result::Long(context, column->unchecked_data<int64_t>()[row]);
    return;
  }
  if (column->type.Is<core::Double>()) {
    sqlite::result::Double(context, column->unchecked_data<double>()[row]);
    return;
  }
  StringPool::Id id = column->unchecked_data<StringPool::Id>()[row];
  if (id.is_null()) {
    sqlite::result::Null(context);
    return;
  }
  const auto value = pool.Get(id);
  // Static (no copy) is safe: StringPool storage is never freed while the
  // connection is alive.
  sqlite::result::StaticString(context, value.c_str(),
                               static_cast<int>(value.size()));
}

}  // namespace

void FlamegraphFindFunction::Step(sqlite3_context* context,
                                  int,
                                  sqlite3_value**) {
  sqlite::result::Error(
      context, "__intrinsic_flamegraph_find must constrain a flamegraph");
}

void FlamegraphConfigFunction::Step(sqlite3_context* context,
                                    int argc,
                                    sqlite3_value** argv) {
  FlamegraphQuery query;
  base::Status status = sqlite::ParseTaggedArgs(
      kName, argc, argv,
      {
          {"view", 1,
           [&](sqlite3_value** values) -> base::Status {
             using Config = flamegraph::Config;
             ASSIGN_OR_RETURN(
                 query.view,
                 ParseToken<Config::View>(
                     "flamegraph_config: view", values[0],
                     {{"TOP_DOWN", Config::View(Config::TopDown{})},
                      {"BOTTOM_UP", Config::View(Config::BottomUp{})},
                      {"PIVOT", Config::View(Config::Pivot{})},
                      {"FROM_FRAME", Config::View(Config::FromFrame{})}}));
             return base::OkStatus();
           }},
          {"view_pattern", 2,
           [&](sqlite3_value** values) -> base::Status {
             ASSIGN_OR_RETURN(query.view_pattern,
                              ParseRegexSpec("flamegraph_config: view_pattern",
                                             values[0], values[1]));
             return base::OkStatus();
           }},
          {"filter", 3,
           [&](sqlite3_value** values) -> base::Status {
             using Kind = FlamegraphFilterSpec::Kind;
             ASSIGN_OR_RETURN(
                 Kind kind,
                 ParseToken<Kind>("flamegraph_config: filter", values[0],
                                  {{"SHOW_STACK", Kind::kShowStack},
                                   {"HIDE_STACK", Kind::kHideStack},
                                   {"HIDE_FRAME", Kind::kHideFrame}}));
             ASSIGN_OR_RETURN(auto regex,
                              ParseRegexSpec("flamegraph_config: filter",
                                             values[1], values[2]));
             query.filters.push_back({kind, std::move(regex)});
             return base::OkStatus();
           }},
          {"grouping", 1,
           [&](sqlite3_value** values) -> base::Status {
             ASSIGN_OR_RETURN(std::string name,
                              sqlite::TaggedArgText(
                                  "flamegraph_config: grouping", values[0]));
             query.grouping_columns.push_back(std::move(name));
             return base::OkStatus();
           }},
          {"value", 1,
           [&](sqlite3_value** values) -> base::Status {
             ASSIGN_OR_RETURN(
                 std::string name,
                 sqlite::TaggedArgText("flamegraph_config: value", values[0]));
             query.value_columns.push_back(std::move(name));
             return base::OkStatus();
           }},
          {"aggregate", 3,
           [&](sqlite3_value** values) -> base::Status {
             using Aggregate = flamegraph::Config::Aggregate;
             ASSIGN_OR_RETURN(
                 Aggregate aggregate,
                 ParseToken<Aggregate>(
                     "flamegraph_config: aggregate", values[0],
                     {{"SUM", Aggregate::kSum},
                      {"ONE_OR_SUMMARY", Aggregate::kOneOrSummary},
                      {"CONCAT_WITH_COMMA", Aggregate::kConcatWithComma}}));
             ASSIGN_OR_RETURN(std::string input,
                              sqlite::TaggedArgText(
                                  "flamegraph_config: aggregate", values[1]));
             ASSIGN_OR_RETURN(std::string output,
                              sqlite::TaggedArgText(
                                  "flamegraph_config: aggregate", values[2]));
             query.aggregate_columns.push_back(
                 {aggregate, std::move(input), std::move(output)});
             return base::OkStatus();
           }},
      });
  if (!status.ok()) {
    sqlite::utils::SetError(context, status);
    return;
  }
  const bool is_pattern_view =
      query.view.IsAnyOf<flamegraph::Config::PatternViews>();
  if (is_pattern_view != query.view_pattern.has_value()) {
    sqlite::utils::SetError(
        context,
        "flamegraph_config: PIVOT and FROM_FRAME require a view pattern");
    return;
  }
  sqlite::utils::MovePointerResult(context, std::move(query),
                                   kConfigPointerType);
}

int FlamegraphOperator::Create(sqlite3* db,
                               void* raw_context,
                               int argc,
                               const char* const* argv,
                               sqlite3_vtab** output,
                               char** error) {
  if (argc != 4) {
    *error = sqlite3_mprintf("flamegraph: wrong number of arguments");
    return SQLITE_ERROR;
  }
  auto* context = GetContext(raw_context);
  auto state = LoadSource(context, argv[3]);
  if (!state.ok()) {
    *error = sqlite3_mprintf("%s", state.status().c_message());
    return SQLITE_ERROR;
  }
  if (int result = DeclareSchema(db, **state, error); result != SQLITE_OK) {
    return result;
  }
  std::unique_ptr<Vtab> vtab = std::make_unique<Vtab>();
  vtab->state = context->OnCreate(argc, argv, std::move(*state));
  *output = vtab.release();
  return SQLITE_OK;
}

int FlamegraphOperator::Destroy(sqlite3_vtab* raw_vtab) {
  std::unique_ptr<Vtab> vtab(GetVtab(raw_vtab));
  sqlite::ModuleStateManager<FlamegraphOperator>::OnDestroy(vtab->state);
  return SQLITE_OK;
}

int FlamegraphOperator::Connect(sqlite3* db,
                                void* raw_context,
                                int argc,
                                const char* const* argv,
                                sqlite3_vtab** output,
                                char** error) {
  auto* context = GetContext(raw_context);
  auto* managed_state = context->OnConnect(argc, argv);
  if (!managed_state) {
    *error = sqlite3_mprintf("flamegraph: state not found");
    return SQLITE_ERROR;
  }
  State* state =
      sqlite::ModuleStateManager<FlamegraphOperator>::GetState(managed_state);
  if (int result = DeclareSchema(db, *state, error); result != SQLITE_OK) {
    return result;
  }
  std::unique_ptr<Vtab> vtab = std::make_unique<Vtab>();
  vtab->state = managed_state;
  *output = vtab.release();
  return SQLITE_OK;
}

int FlamegraphOperator::Disconnect(sqlite3_vtab* raw_vtab) {
  std::unique_ptr<Vtab> vtab(GetVtab(raw_vtab));
  return SQLITE_OK;
}

int FlamegraphOperator::BestIndex(sqlite3_vtab*, sqlite3_index_info* info) {
  base::Status status = sqlite::utils::ValidateFunctionArguments(
      info, kArgCount, IsArgumentColumn);
  if (!status.ok()) {
    return SQLITE_CONSTRAINT;
  }
  for (int i = 0; i < info->nConstraint; ++i) {
    const auto& input = info->aConstraint[i];
    if (input.usable && input.iColumn == kTreeIdColumn &&
        input.op == kSuperRootConstraint) {
      info->aConstraintUsage[i].argvIndex = 2;
      info->aConstraintUsage[i].omit = true;
      info->idxNum = kSuperRootPlan;
      break;
    }
  }
  return SQLITE_OK;
}

int FlamegraphOperator::Open(sqlite3_vtab*, sqlite3_vtab_cursor** output) {
  std::unique_ptr<Cursor> cursor = std::make_unique<Cursor>();
  *output = cursor.release();
  return SQLITE_OK;
}

int FlamegraphOperator::Close(sqlite3_vtab_cursor* raw_cursor) {
  std::unique_ptr<Cursor> cursor(GetCursor(raw_cursor));
  return SQLITE_OK;
}

int FlamegraphOperator::Filter(sqlite3_vtab_cursor* raw_cursor,
                               int plan,
                               const char*,
                               int argc,
                               sqlite3_value** argv) {
  Cursor* cursor = GetCursor(raw_cursor);
  Vtab* vtab = GetVtab(cursor->pVtab);
  State* state =
      sqlite::ModuleStateManager<FlamegraphOperator>::GetState(vtab->state);
  PERFETTO_CHECK(argc == kArgCount ||
                 (plan == kSuperRootPlan && argc == kArgCount + 1));
  cursor->result.reset();
  cursor->columns.clear();
  cursor->layout = {};
  cursor->row = 0;
  cursor->super_root_only = plan == kSuperRootPlan;
  if (cursor->super_root_only) {
    const unsigned char* selector = sqlite3_value_text(argv[1]);
    if (!selector ||
        strcmp(reinterpret_cast<const char*>(selector), "SUPER_ROOT") != 0) {
      return sqlite::utils::SetError(vtab, "flamegraph: unknown find selector");
    }
  }

  auto query = sqlite::utils::TakeMovePointerValue<FlamegraphQuery>(
      argv[0], kConfigPointerType, "flamegraph");
  if (!query.ok()) {
    return sqlite::utils::SetError(vtab, query.status());
  }
  if (query->value_columns.empty()) {
    return sqlite::utils::SetError(
        vtab, "flamegraph: at least one value column is required");
  }
  if (cursor->super_root_only || state->source.row_count == 0) {
    return SQLITE_OK;
  }

  auto config = ResolveConfig(state->source, *state->pool, *query);
  if (!config.ok()) {
    return sqlite::utils::SetError(vtab, config.status());
  }
  // TODO: Cache metric-independent intermediate results across metric queries,
  // not complete query results. In particular, preserve the computed tree shape
  // when only the value columns change so switching metrics only recomputes
  // metric-dependent values.
  auto result = flamegraph::Build(state->source, *config);
  if (!result.ok()) {
    return sqlite::utils::SetError(vtab, result.status());
  }
  cursor->result = std::make_unique<core::Tree>(std::move(*result));

  // Resolve every schema column against the result tree once so that Column()
  // is a plain array lookup per cell.
  const core::Tree& tree = *cursor->result;
  cursor->columns.assign(state->output_column_count, nullptr);
  auto resolve = [&](uint32_t index, const std::string& name) {
    auto found = tree.Find(name);
    cursor->columns[index] = found ? *found : nullptr;
  };
  const std::string& primary = query->value_columns[0];
  resolve(kDepthColumn, "depth");
  resolve(kNameColumn, "name");
  resolve(kSelfValueColumn, "self_" + primary);
  resolve(kCumulativeValueColumn, "cumulative_" + primary);
  for (uint32_t i = 0; i < state->property_names.size(); ++i) {
    resolve(kPropertyColumnStart + i, state->property_names[i]);
  }
  for (uint32_t i = 0; i < state->candidate_value_names.size(); ++i) {
    const std::string& name = state->candidate_value_names[i];
    resolve(state->candidate_column_start + 2 * i, "self_" + name);
    resolve(state->candidate_column_start + 2 * i + 1, "cumulative_" + name);
  }

  const core::Tree::Column* cumulative =
      cursor->columns[kCumulativeValueColumn];
  const core::Tree::Column* depth = cursor->columns[kDepthColumn];
  PERFETTO_CHECK(cumulative && depth);
  cursor->value_is_int = cumulative->type.Is<core::Int64>();
  cursor->layout = flamegraph::ComputeLayout(tree, *cumulative, *depth);
  return SQLITE_OK;
}

int FlamegraphOperator::Next(sqlite3_vtab_cursor* raw_cursor) {
  GetCursor(raw_cursor)->row++;
  return SQLITE_OK;
}

int FlamegraphOperator::Eof(sqlite3_vtab_cursor* raw_cursor) {
  Cursor* cursor = GetCursor(raw_cursor);
  if (cursor->super_root_only) {
    return cursor->row > 0;
  }
  return cursor->row >= cursor->layout.node.size();
}

int FlamegraphOperator::Column(sqlite3_vtab_cursor* raw_cursor,
                               sqlite3_context* context,
                               int raw_column) {
  Cursor* cursor = GetCursor(raw_cursor);
  Vtab* vtab = GetVtab(cursor->pVtab);
  State* state =
      sqlite::ModuleStateManager<FlamegraphOperator>::GetState(vtab->state);
  const auto column = static_cast<uint32_t>(raw_column);
  if (cursor->super_root_only) {
    if (column == kTreeIdColumn) {
      sqlite::result::Long(context, 0);
    } else if (column == kDepthColumn) {
      sqlite::result::Long(context, 0);
    } else if (column == kCumulativeValueColumn) {
      sqlite::result::Double(context, state->source_value_sum);
    } else {
      sqlite::result::Null(context);
    }
    return SQLITE_OK;
  }
  const uint32_t row = cursor->row;
  switch (column) {
    case kTreeIdColumn:
      sqlite::result::Long(context, row);
      return SQLITE_OK;
    case kTreeParentIdColumn: {
      const uint32_t parent = cursor->layout.parent_row[row];
      if (parent == core::Tree::kNullParent) {
        sqlite::result::Null(context);
      } else {
        sqlite::result::Long(context, parent);
      }
      return SQLITE_OK;
    }
    case kParentCumulativeValueColumn: {
      const uint32_t parent = cursor->layout.parent_row[row];
      if (parent == core::Tree::kNullParent) {
        sqlite::result::Null(context);
      } else {
        ReturnTreeColumn(context, *state->pool,
                         cursor->columns[kCumulativeValueColumn],
                         cursor->layout.node[parent]);
      }
      return SQLITE_OK;
    }
    case kXStartColumn:
    case kXEndColumn: {
      double x = cursor->layout.x_start[row];
      if (column == kXEndColumn) {
        // A node's width is its cumulative value clamped to zero.
        const core::Tree::Column* cumulative =
            cursor->columns[kCumulativeValueColumn];
        const uint32_t node = cursor->layout.node[row];
        const double value =
            cursor->value_is_int
                ? static_cast<double>(
                      cumulative->unchecked_data<int64_t>()[node])
                : cumulative->unchecked_data<double>()[node];
        x += std::max(value, 0.0);
      }
      if (cursor->value_is_int) {
        sqlite::result::Long(context, static_cast<int64_t>(x));
      } else {
        sqlite::result::Double(context, x);
      }
      return SQLITE_OK;
    }
    default:
      break;
  }
  if (column >= cursor->columns.size()) {
    return sqlite::utils::SetError(vtab, "flamegraph: bad column");
  }
  ReturnTreeColumn(context, *state->pool, cursor->columns[column],
                   cursor->layout.node[row]);
  return SQLITE_OK;
}

int FlamegraphOperator::Rowid(sqlite3_vtab_cursor*, sqlite_int64*) {
  return SQLITE_ERROR;
}

int FlamegraphOperator::FindFunction(sqlite3_vtab*,
                                     int,
                                     const char* name,
                                     FindFunctionFn** function,
                                     void**) {
  if (base::CaseInsensitiveEqual(name, FlamegraphFindFunction::kName)) {
    *function = [](sqlite3_context* context, int, sqlite3_value**) {
      sqlite::result::Error(context, "super-root constraint was not consumed");
    };
    return kSuperRootConstraint;
  }
  return SQLITE_OK;
}

namespace flamegraph {
namespace {

class FlamegraphPlugin : public Plugin<FlamegraphPlugin> {
 public:
  ~FlamegraphPlugin() override;

  void RegisterFunctions(PerfettoSqlConnection*,
                         std::vector<FunctionRegistration>& out) override {
    StringPool* pool = trace_context_->storage->mutable_string_pool();
    out.push_back(MakeFunctionRegistration<FlamegraphConfigFunction>(pool));
    out.push_back(MakeFunctionRegistration<FlamegraphFindFunction>(nullptr));
  }

  void RegisterSqliteModules(
      PerfettoSqlConnection* connection,
      std::vector<SqliteModuleRegistration>& out) override {
    StringPool* pool = trace_context_->storage->mutable_string_pool();
    out.push_back(MakeSqliteModule<FlamegraphOperator>(
        "__intrinsic_flamegraph",
        std::make_unique<FlamegraphOperator::Context>(connection, pool)));
  }
};

FlamegraphPlugin::~FlamegraphPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration registration(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<FlamegraphPlugin>();
      },
      FlamegraphPlugin::kPluginId, FlamegraphPlugin::kDepIds.data(),
      FlamegraphPlugin::kDepIds.size());
  base::ignore_result(registration);
}

}  // namespace flamegraph
}  // namespace perfetto::trace_processor
