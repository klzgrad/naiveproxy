/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/generator/structured_query_generator.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "protos/perfetto/perfetto_sql/structured_query.pbzero.h"
#include "src/trace_processor/perfetto_sql/tokenizer/sqlite_tokenizer.h"
#include "src/trace_processor/sqlite/sql_source.h"

namespace perfetto::trace_processor::perfetto_sql::generator {

namespace {

using StructuredQuery = protos::pbzero::PerfettoSqlStructuredQuery;

enum QueryType : uint8_t {
  kRoot,
  kShared,
  kNested,
};

std::pair<SqlSource, SqlSource> GetPreambleAndSql(const std::string& sql) {
  std::pair<SqlSource, SqlSource> result{
      SqlSource(SqlSource::FromTraceProcessorImplementation("")),
      SqlSource(SqlSource::FromTraceProcessorImplementation(""))};

  if (sql.empty()) {
    return result;
  }

  SqliteTokenizer tokenizer(SqlSource::FromTraceProcessorImplementation(sql));

  // Skip any leading semicolons.
  SqliteTokenizer::Token first_tok = tokenizer.NextNonWhitespace();
  while (first_tok.token_type == TK_SEMI) {
    first_tok = tokenizer.NextNonWhitespace();
  }

  // If there are no statements, return empty.
  if (first_tok.IsTerminal()) {
    return result;
  }

  SqliteTokenizer::Token last_statement_start = first_tok;
  SqliteTokenizer::Token statement_end = first_tok;

  // Find the start of the last statement.
  while (true) {
    // Find the end of the current statement.
    SqliteTokenizer::Token end = tokenizer.NextTerminal();

    // If that was the end of the SQL, we're done.
    if (end.str.empty()) {
      statement_end = end;
      break;
    }

    // Otherwise, find the start of the next statement.
    SqliteTokenizer::Token next_start = tokenizer.NextNonWhitespace();
    while (next_start.token_type == TK_SEMI) {
      next_start = tokenizer.NextNonWhitespace();
    }

    // If there is no next statement, we're done.
    if (next_start.IsTerminal()) {
      statement_end = end;
      break;
    }

    // Otherwise, the next statement is now our candidate for the last
    // statement.
    last_statement_start = next_start;
  }

  return {tokenizer.Substr(first_tok, last_statement_start),
          tokenizer.Substr(last_statement_start, statement_end)};
}

// Indents each line of the input string by the specified number of spaces.
std::string IndentLines(const std::string& input, size_t indent_spaces) {
  if (input.empty()) {
    return input;
  }

  std::string result;
  result.reserve(input.size() + indent_spaces * 10);  // Estimate

  std::string indent(indent_spaces, ' ');
  size_t pos = 0;
  size_t line_start = 0;

  while (pos < input.size()) {
    if (input[pos] == '\n') {
      result.append(indent);
      result.append(input, line_start, pos - line_start + 1);
      line_start = pos + 1;
    }
    pos++;
  }

  // Handle last line if it doesn't end with newline
  if (line_start < input.size()) {
    result.append(indent);
    result.append(input, line_start, std::string::npos);
  }

  return result;
}

struct QueryState {
  QueryState(QueryType _type,
             protozero::ConstBytes _bytes,
             size_t index,
             std::optional<size_t> parent_idx,
             std::set<std::string>& used_table_names)
      : type(_type), bytes(_bytes), parent_index(parent_idx) {
    protozero::ProtoDecoder decoder(bytes);
    std::string prefix = type == QueryType::kShared ? "shared_sq_" : "sq_";
    if (auto id = decoder.FindField(StructuredQuery::kIdFieldNumber); id) {
      id_from_proto = id.as_std_string();
      table_name = prefix + *id_from_proto;
    } else {
      table_name = prefix + std::to_string(index);
    }

    // Ensure table_name is unique by appending a suffix if needed
    std::string original_name = table_name;
    size_t suffix = 0;
    while (used_table_names.count(table_name) > 0) {
      table_name = original_name + "_" + std::to_string(suffix);
      suffix++;
    }
    used_table_names.insert(table_name);
  }

  QueryType type;
  protozero::ConstBytes bytes;
  std::optional<std::string> id_from_proto;
  std::string table_name;
  std::optional<size_t> parent_index;

  std::string sql;
};

using Query = StructuredQueryGenerator::Query;
using QueryProto = StructuredQueryGenerator::QueryProto;

class GeneratorImpl {
 public:
  GeneratorImpl(base::FlatHashMap<std::string, QueryProto>& protos,
                std::vector<Query>& queries,
                base::FlatHashMap<std::string, std::nullptr_t>& modules,
                std::vector<std::string>& preambles)
      : query_protos_(protos),
        queries_(queries),
        referenced_modules_(modules),
        preambles_(preambles) {}

  base::StatusOr<std::string> Generate(protozero::ConstBytes,
                                       bool inline_shared_queries);

 private:
  using RepeatedString =
      protozero::RepeatedFieldIterator<protozero::ConstChars>;
  using RepeatedProto = protozero::RepeatedFieldIterator<protozero::ConstBytes>;

  base::StatusOr<std::string> GenerateImpl();

  // Base sources
  base::StatusOr<std::string> Table(const StructuredQuery::Table::Decoder&);
  base::StatusOr<std::string> SimpleSlices(
      const StructuredQuery::SimpleSlices::Decoder&);
  base::StatusOr<std::string> SqlSource(const StructuredQuery::Sql::Decoder&);
  base::StatusOr<std::string> TimeRange(
      const StructuredQuery::ExperimentalTimeRange::Decoder&);

  // Nested sources
  std::string NestedSource(protozero::ConstBytes);
  base::StatusOr<std::string> ReferencedSharedQuery(
      protozero::ConstChars raw_id);

  base::StatusOr<std::string> IntervalIntersect(
      const StructuredQuery::IntervalIntersect::Decoder&);

  base::StatusOr<std::string> FilterToIntervals(
      const StructuredQuery::ExperimentalFilterToIntervals::Decoder&);

  base::StatusOr<std::string> Join(
      const StructuredQuery::ExperimentalJoin::Decoder&);

  base::StatusOr<std::string> Union(
      const StructuredQuery::ExperimentalUnion::Decoder&);

  base::StatusOr<std::string> AddColumns(
      const StructuredQuery::ExperimentalAddColumns::Decoder&);

  base::StatusOr<std::string> CreateSlices(
      const StructuredQuery::ExperimentalCreateSlices::Decoder&);

  base::StatusOr<std::string> CounterIntervals(
      const StructuredQuery::ExperimentalCounterIntervals::Decoder&);

  base::StatusOr<std::string> FilterIn(
      const StructuredQuery::ExperimentalFilterIn::Decoder&);

  // Filtering.
  static base::StatusOr<std::string> Filters(RepeatedProto filters);
  static base::StatusOr<std::string> ExperimentalFilterGroup(
      const StructuredQuery::ExperimentalFilterGroup::Decoder&);
  static base::StatusOr<std::string> SingleFilter(
      const StructuredQuery::Filter::Decoder&);

  // Aggregation.
  static base::StatusOr<std::string> GroupBy(RepeatedString group_by);
  static base::StatusOr<std::string> SelectColumnsAggregates(
      RepeatedString group_by,
      RepeatedProto aggregates,
      RepeatedProto select_cols);
  base::StatusOr<std::string> SelectColumnsNoAggregates(
      RepeatedProto select_columns);

  // Sorting.
  static base::StatusOr<std::string> OrderBy(
      const StructuredQuery::OrderBy::Decoder&);

  // Helpers.
  static base::StatusOr<std::string> OperatorToString(
      StructuredQuery::Filter::Operator op);
  static base::StatusOr<std::string> AggregateToString(
      const StructuredQuery::GroupBy::Aggregate::Decoder&);

  // Index of the current query we are processing in the `state_` vector.
  size_t state_index_ = 0;
  std::vector<QueryState> state_;
  base::FlatHashMap<std::string, QueryProto>& query_protos_;
  std::vector<Query>& queries_;
  base::FlatHashMap<std::string, std::nullptr_t>& referenced_modules_;
  std::vector<std::string>& preambles_;
  std::set<std::string> used_table_names_;
};

base::StatusOr<std::string> GeneratorImpl::Generate(
    protozero::ConstBytes bytes,
    bool inline_shared_queries) {
  state_.emplace_back(QueryType::kRoot, bytes, state_.size(), std::nullopt,
                      used_table_names_);
  for (; state_index_ < state_.size(); ++state_index_) {
    base::StatusOr<std::string> sql = GenerateImpl();
    if (!sql.ok()) {
      return base::ErrStatus(
          "Failed to generate SQL for query (id=%s, idx=%zu): %s",
          state_[state_index_].id_from_proto.value_or("unknown").c_str(),
          state_index_, sql.status().c_message());
    }
    state_[state_index_].sql = *sql;
  }

  // Check if the root query is just an inner_query wrapper with operations
  // (ORDER BY, LIMIT, OFFSET). If so, we should apply those in the final
  // SELECT instead of creating a duplicate CTE.
  StructuredQuery::Decoder root_query(state_[0].bytes);
  bool root_only_has_inner_query_and_operations =
      root_query.has_inner_query() && !root_query.has_table() &&
      !root_query.has_experimental_time_range() &&
      !root_query.has_simple_slices() && !root_query.has_interval_intersect() &&
      !root_query.has_experimental_filter_to_intervals() &&
      !root_query.has_experimental_join() &&
      !root_query.has_experimental_union() && !root_query.has_sql() &&
      !root_query.has_inner_query_id() && !root_query.filters() &&
      !root_query.has_experimental_filter_group() &&
      !root_query.has_group_by() && !root_query.select_columns() &&
      !root_query.has_experimental_add_columns() &&
      !root_query.has_experimental_create_slices() &&
      !root_query.has_experimental_counter_intervals() &&
      !root_query.has_experimental_filter_in();

  std::string sql = "WITH ";
  size_t cte_count = 0;
  for (size_t i = 0; i < state_.size(); ++i) {
    QueryState& state = state_[state_.size() - i - 1];
    if (state.type == QueryType::kShared) {
      // When not inlining shared queries, add them to referenced_queries
      // so callers can create tables for them separately.
      if (!inline_shared_queries) {
        queries_.emplace_back(
            Query{state.id_from_proto.value(), state.table_name, state.sql});
        continue;
      }
      // When inlining, fall through to add as CTE below.
    }
    // Skip the root query if it's just a wrapper for inner_query + operations
    if (&state == &state_[0] && root_only_has_inner_query_and_operations) {
      continue;
    }
    if (cte_count > 0) {
      sql += ",\n";
    }
    sql += state.table_name + " AS (\n" + IndentLines(state.sql, 2) + "\n)";
    cte_count++;
  }

  // Build the final SELECT
  if (root_only_has_inner_query_and_operations) {
    // The root query is just wrapping an inner query with operations.
    // Apply those operations directly in the final SELECT.
    sql += "\n" + state_[0].sql;
  } else {
    sql += "\nSELECT *\nFROM " + state_[0].table_name;
  }
  return sql;
}

base::StatusOr<std::string> GeneratorImpl::GenerateImpl() {
  StructuredQuery::Decoder q(state_[state_index_].bytes);

  for (auto it = q.referenced_modules(); it; ++it) {
    referenced_modules_.Insert(it->as_std_string(), nullptr);
  }

  // Warning: do *not* keep a reference to elements in `state_` across any of
  // these functions: `state_` can be modified by them.
  std::string source;
  {
    if (q.has_table()) {
      StructuredQuery::Table::Decoder table(q.table());
      ASSIGN_OR_RETURN(source, Table(table));
    } else if (q.has_experimental_time_range()) {
      StructuredQuery::ExperimentalTimeRange::Decoder time_range(
          q.experimental_time_range());
      ASSIGN_OR_RETURN(source, TimeRange(time_range));
    } else if (q.has_simple_slices()) {
      StructuredQuery::SimpleSlices::Decoder slices(q.simple_slices());
      ASSIGN_OR_RETURN(source, SimpleSlices(slices));
    } else if (q.has_interval_intersect()) {
      StructuredQuery::IntervalIntersect::Decoder ii(q.interval_intersect());
      ASSIGN_OR_RETURN(source, IntervalIntersect(ii));
    } else if (q.has_experimental_filter_to_intervals()) {
      StructuredQuery::ExperimentalFilterToIntervals::Decoder fti(
          q.experimental_filter_to_intervals());
      ASSIGN_OR_RETURN(source, FilterToIntervals(fti));
    } else if (q.has_experimental_join()) {
      StructuredQuery::ExperimentalJoin::Decoder join(q.experimental_join());
      ASSIGN_OR_RETURN(source, Join(join));
    } else if (q.has_experimental_union()) {
      StructuredQuery::ExperimentalUnion::Decoder union_decoder(
          q.experimental_union());
      ASSIGN_OR_RETURN(source, Union(union_decoder));

    } else if (q.has_experimental_add_columns()) {
      StructuredQuery::ExperimentalAddColumns::Decoder add_columns_decoder(
          q.experimental_add_columns());
      ASSIGN_OR_RETURN(source, AddColumns(add_columns_decoder));
    } else if (q.has_experimental_create_slices()) {
      StructuredQuery::ExperimentalCreateSlices::Decoder create_slices_decoder(
          q.experimental_create_slices());
      ASSIGN_OR_RETURN(source, CreateSlices(create_slices_decoder));
    } else if (q.has_experimental_counter_intervals()) {
      StructuredQuery::ExperimentalCounterIntervals::Decoder
          counter_intervals_decoder(q.experimental_counter_intervals());
      ASSIGN_OR_RETURN(source, CounterIntervals(counter_intervals_decoder));
    } else if (q.has_experimental_filter_in()) {
      StructuredQuery::ExperimentalFilterIn::Decoder filter_in_decoder(
          q.experimental_filter_in());
      ASSIGN_OR_RETURN(source, FilterIn(filter_in_decoder));
    } else if (q.has_sql()) {
      StructuredQuery::Sql::Decoder sql_source(q.sql());
      ASSIGN_OR_RETURN(source, SqlSource(sql_source));
    } else if (q.has_inner_query()) {
      source = NestedSource(q.inner_query());
    } else if (q.has_inner_query_id()) {
      ASSIGN_OR_RETURN(source, ReferencedSharedQuery(q.inner_query_id()));
    } else {
      return base::ErrStatus("Query must specify a source");
    }
  }

  std::string filters;
  if (q.has_experimental_filter_group()) {
    StructuredQuery::ExperimentalFilterGroup::Decoder exp_filter_group(
        q.experimental_filter_group());
    ASSIGN_OR_RETURN(filters, ExperimentalFilterGroup(exp_filter_group));
  } else {
    ASSIGN_OR_RETURN(filters, Filters(q.filters()));
  }

  std::string select;
  std::string group_by;
  if (q.has_group_by()) {
    StructuredQuery::GroupBy::Decoder gb(q.group_by());
    ASSIGN_OR_RETURN(group_by, GroupBy(gb.column_names()));
    ASSIGN_OR_RETURN(select,
                     SelectColumnsAggregates(gb.column_names(), gb.aggregates(),
                                             q.select_columns()));
  } else {
    ASSIGN_OR_RETURN(select, SelectColumnsNoAggregates(q.select_columns()));
  }

  // Assemble SQL clauses in standard evaluation order:
  // SELECT, FROM, WHERE, GROUP BY, ORDER BY, LIMIT, OFFSET.
  std::string sql = "SELECT " + select + "\nFROM " + source;
  if (!filters.empty()) {
    sql += "\nWHERE " + filters;
  }
  if (!group_by.empty()) {
    sql += "\n" + group_by;
  }
  if (q.has_order_by()) {
    StructuredQuery::OrderBy::Decoder order_by_decoder(q.order_by());
    ASSIGN_OR_RETURN(std::string order_by, OrderBy(order_by_decoder));
    sql += "\n" + order_by;
  }
  if (q.has_offset() && !q.has_limit()) {
    return base::ErrStatus("OFFSET requires LIMIT to be specified");
  }
  if (q.has_limit()) {
    if (q.limit() < 0) {
      return base::ErrStatus("LIMIT must be non-negative, got %" PRId64,
                             q.limit());
    }
    sql += "\nLIMIT " + std::to_string(q.limit());
  }
  if (q.has_offset()) {
    if (q.offset() < 0) {
      return base::ErrStatus("OFFSET must be non-negative, got %" PRId64,
                             q.offset());
    }
    sql += "\nOFFSET " + std::to_string(q.offset());
  }
  return sql;
}

base::StatusOr<std::string> GeneratorImpl::Table(
    const StructuredQuery::Table::Decoder& table) {
  if (table.table_name().size == 0) {
    return base::ErrStatus("Table must specify a table name");
  }
  if (table.module_name().size > 0) {
    referenced_modules_.Insert(table.module_name().ToStdString(), nullptr);
  }
  return table.table_name().ToStdString();
}

base::StatusOr<std::string> GeneratorImpl::TimeRange(
    const StructuredQuery::ExperimentalTimeRange::Decoder& time_range) {
  if (!time_range.has_mode()) {
    return base::ErrStatus("ExperimentalTimeRange: mode field is required");
  }

  switch (time_range.mode()) {
    case StructuredQuery::ExperimentalTimeRange::STATIC: {
      if (!time_range.has_ts()) {
        return base::ErrStatus(
            "ExperimentalTimeRange: ts is required for STATIC mode");
      }
      if (!time_range.has_dur()) {
        return base::ErrStatus(
            "ExperimentalTimeRange: dur is required for STATIC mode");
      }
      std::string ts_expr = std::to_string(time_range.ts());
      std::string dur_expr = std::to_string(time_range.dur());
      return "(SELECT 0 AS id, " + ts_expr + " AS ts, " + dur_expr + " AS dur)";
    }
    case StructuredQuery::ExperimentalTimeRange::DYNAMIC: {
      std::string ts_expr = time_range.has_ts()
                                ? std::to_string(time_range.ts())
                                : "trace_start()";
      std::string dur_expr = time_range.has_dur()
                                 ? std::to_string(time_range.dur())
                                 : "trace_dur()";
      return "(SELECT 0 AS id, " + ts_expr + " AS ts, " + dur_expr + " AS dur)";
    }
  }
  return base::ErrStatus("ExperimentalTimeRange: unknown mode value");
}

base::StatusOr<std::string> GeneratorImpl::SqlSource(
    const StructuredQuery::Sql::Decoder& sql) {
  if (sql.sql().size == 0) {
    return base::ErrStatus("Sql field must be specified");
  }

  class SqlSource source_sql =
      SqlSource::FromTraceProcessorImplementation(sql.sql().ToStdString());
  class SqlSource final_sql_statement =
      SqlSource::FromTraceProcessorImplementation("");
  if (sql.has_preamble()) {
    // If preambles are specified, we assume that the SQL is a single statement.
    auto [parsed_preamble, main_sql] = GetPreambleAndSql(source_sql.sql());
    if (!parsed_preamble.sql().empty()) {
      return base::ErrStatus(
          "Sql source specifies both `preamble` and has multiple statements in "
          "the `sql` field. This is not supported - please don't use "
          "`preamble` "
          "and pass all the SQL you want to execute in the `sql` field.");
    }
    preambles_.push_back(sql.preamble().ToStdString());
    final_sql_statement = source_sql;
  } else {
    auto [parsed_preamble, main_sql] = GetPreambleAndSql(source_sql.sql());
    if (!parsed_preamble.sql().empty()) {
      preambles_.push_back(parsed_preamble.sql());
    }
    final_sql_statement = main_sql;
  }

  if (final_sql_statement.sql().empty()) {
    return base::ErrStatus(
        "SQL source cannot be empty after processing preamble");
  }

  SqlSource::Rewriter rewriter(final_sql_statement);
  for (auto it = sql.dependencies(); it; ++it) {
    StructuredQuery::Sql::Dependency::Decoder dependency(*it);
    std::string alias = dependency.alias().ToStdString();
    std::string inner_query_name = NestedSource(dependency.query());

    SqliteTokenizer tokenizer(final_sql_statement);
    for (auto token = tokenizer.Next(); !token.str.empty();
         token = tokenizer.Next()) {
      if (token.token_type == TK_VARIABLE && token.str.substr(1) == alias) {
        tokenizer.RewriteToken(
            rewriter, token,
            SqlSource::FromTraceProcessorImplementation(inner_query_name));
      }
    }
  }

  std::string cols_str = "*";
  if (sql.column_names()->size() != 0) {
    std::vector<std::string> cols;
    for (auto it = sql.column_names(); it; ++it) {
      cols.push_back(it->as_std_string());
    }
    cols_str = base::Join(cols, ", ");
  }

  std::string user_sql = std::move(rewriter).Build().sql();
  std::string inner =
      "SELECT " + cols_str + "\nFROM (\n" + IndentLines(user_sql, 2) + "\n)";
  std::string generated_sql = "(\n" + IndentLines(inner, 2) + ")";
  return generated_sql;
}

base::StatusOr<std::string> GeneratorImpl::SimpleSlices(
    const StructuredQuery::SimpleSlices::Decoder& slices) {
  referenced_modules_.Insert("slices.with_context", nullptr);

  std::string sql =
      "SELECT id, ts, dur, name AS slice_name, thread_name, process_name, "
      "track_name FROM thread_or_process_slice";

  std::vector<std::string> conditions;
  if (slices.has_slice_name_glob()) {
    conditions.push_back("slice_name GLOB '" +
                         slices.slice_name_glob().ToStdString() + "'");
  }
  if (slices.has_thread_name_glob()) {
    conditions.push_back("thread_name GLOB '" +
                         slices.thread_name_glob().ToStdString() + "'");
  }
  if (slices.has_process_name_glob()) {
    conditions.push_back("process_name GLOB '" +
                         slices.process_name_glob().ToStdString() + "'");
  }
  if (slices.has_track_name_glob()) {
    conditions.push_back("track_name GLOB '" +
                         slices.track_name_glob().ToStdString() + "'");
  }
  if (!conditions.empty()) {
    sql += " WHERE " + conditions[0];
    for (size_t i = 1; i < conditions.size(); ++i) {
      sql += " AND " + conditions[i];
    }
  }
  return "(" + sql + ")";
}

base::StatusOr<std::string> GeneratorImpl::IntervalIntersect(
    const StructuredQuery::IntervalIntersect::Decoder& interval) {
  if (interval.base().size == 0) {
    return base::ErrStatus("IntervalIntersect must specify a base query");
  }
  if (!interval.interval_intersect()) {
    return base::ErrStatus(
        "IntervalIntersect must specify at least one interval query");
  }
  referenced_modules_.Insert("intervals.intersect", nullptr);

  // Validate and collect partition columns
  std::vector<std::string> partition_cols;
  std::set<std::string> seen_cols;
  for (auto it = interval.partition_columns(); it; ++it) {
    std::string col = it->as_std_string();

    // Validate that partition columns are not empty
    if (col.empty()) {
      return base::ErrStatus("Partition column cannot be empty");
    }

    // Validate that partition columns are not id, ts, or dur (case-insensitive)
    if (base::CaseInsensitiveEqual(col, "id") ||
        base::CaseInsensitiveEqual(col, "ts") ||
        base::CaseInsensitiveEqual(col, "dur")) {
      return base::ErrStatus(
          "Partition column '%s' is reserved and cannot be used for "
          "partitioning",
          col.c_str());
    }

    // Check for duplicates (case-insensitive)
    std::string col_lower = col;
    std::transform(col_lower.begin(), col_lower.end(), col_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (seen_cols.count(col_lower) > 0) {
      return base::ErrStatus("Partition column '%s' is duplicated",
                             col.c_str());
    }
    seen_cols.insert(col_lower);
    partition_cols.push_back(col);
  }

  std::string sql =
      "(WITH iibase AS (SELECT * FROM " + NestedSource(interval.base()) + ")";
  auto ii = interval.interval_intersect();
  for (size_t i = 0; ii; ++ii, ++i) {
    sql += ", iisource" + std::to_string(i) + " AS (SELECT * FROM " +
           NestedSource(*ii) + ")";
  }

  sql += "\nSELECT ii.ts, ii.dur";
  // Add partition columns from ii
  for (const auto& col : partition_cols) {
    sql += ", ii." + col;
  }

  // Add renamed columns from iibase (base table gets _0 suffix)
  // We explicitly rename id, ts, dur for unambiguous access
  sql += ", base_0.id AS id_0, base_0.ts AS ts_0, base_0.dur AS dur_0";
  // Also add all other columns from base table
  sql += ", base_0.*";

  // Add renamed columns from each interval source (they get _1, _2, etc.
  // suffixes)
  ii = interval.interval_intersect();
  for (size_t i = 0; ii; ++ii, ++i) {
    size_t suffix = i + 1;
    sql += ", source_" + std::to_string(suffix) + ".id AS id_" +
           std::to_string(suffix);
    sql += ", source_" + std::to_string(suffix) + ".ts AS ts_" +
           std::to_string(suffix);
    sql += ", source_" + std::to_string(suffix) + ".dur AS dur_" +
           std::to_string(suffix);
    // Also add all other columns from this source table
    sql += ", source_" + std::to_string(suffix) + ".*";
  }

  sql += "\nFROM _interval_intersect!((iibase";
  ii = interval.interval_intersect();
  for (size_t i = 0; ii; ++ii, ++i) {
    sql += ", iisource" + std::to_string(i);
  }

  // Add partition columns to the macro call
  sql += "), (";
  for (size_t i = 0; i < partition_cols.size(); ++i) {
    if (i > 0) {
      sql += ", ";
    }
    sql += partition_cols[i];
  }
  sql += ")) ii\nJOIN iibase AS base_0 ON ii.id_0 = base_0.id";

  ii = interval.interval_intersect();
  for (size_t i = 0; ii; ++ii, ++i) {
    size_t suffix = i + 1;
    sql += "\nJOIN iisource" + std::to_string(i) + " AS source_" +
           std::to_string(suffix) + " ON ii.id_" + std::to_string(suffix) +
           " = source_" + std::to_string(suffix) + ".id";
  }
  sql += ")";

  return sql;
}

base::StatusOr<std::string> GeneratorImpl::FilterToIntervals(
    const StructuredQuery::ExperimentalFilterToIntervals::Decoder& filter) {
  if (filter.base().size == 0) {
    return base::ErrStatus("FilterToIntervals must specify a base query");
  }
  if (filter.intervals().size == 0) {
    return base::ErrStatus("FilterToIntervals must specify an intervals query");
  }
  referenced_modules_.Insert("intervals.intersect", nullptr);
  referenced_modules_.Insert("intervals.overlap", nullptr);

  // Validate and collect partition columns
  std::vector<std::string> partition_cols;
  std::set<std::string> seen_cols;
  for (auto it = filter.partition_columns(); it; ++it) {
    std::string col = it->as_std_string();

    // Validate that partition columns are not empty
    if (col.empty()) {
      return base::ErrStatus("Partition column cannot be empty");
    }

    // Validate that partition columns are not id, ts, or dur (case-insensitive)
    if (base::CaseInsensitiveEqual(col, "id") ||
        base::CaseInsensitiveEqual(col, "ts") ||
        base::CaseInsensitiveEqual(col, "dur")) {
      return base::ErrStatus(
          "Partition column '%s' is reserved and cannot be used for "
          "partitioning",
          col.c_str());
    }

    // Check for duplicates (case-insensitive)
    std::string col_lower = col;
    std::transform(col_lower.begin(), col_lower.end(), col_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (seen_cols.count(col_lower) > 0) {
      return base::ErrStatus("Partition column '%s' is duplicated",
                             col.c_str());
    }
    seen_cols.insert(col_lower);
    partition_cols.push_back(col);
  }

  // Determine if we clip to intervals (default true)
  bool clip_to_intervals =
      filter.has_clip_to_intervals() ? filter.clip_to_intervals() : true;

  // Collect select columns if specified
  std::vector<std::string> select_cols;
  for (auto it = filter.select_columns(); it; ++it) {
    select_cols.push_back(it->as_std_string());
  }

  // Generate the SQL query
  // We first merge overlapping intervals in the filter set, then use
  // _interval_intersect! internally to compute overlaps, and finally
  // reshape the output to match the base schema.
  std::string base_source = NestedSource(filter.base());
  std::string intervals_source = NestedSource(filter.intervals());

  std::string sql = "(WITH fti_base AS (SELECT * FROM " + base_source + ")";
  sql += ",\nfti_intervals_raw AS (SELECT * FROM " + intervals_source + ")";

  // Merge overlapping intervals to avoid duplicate output rows when a base
  // interval overlaps with multiple overlapping filter intervals.
  // Use partitioned merge if we have partition columns, otherwise use
  // non-partitioned merge.
  if (partition_cols.empty()) {
    // Non-partitioned: use interval_merge_overlapping and add a dummy id
    sql +=
        ",\nfti_intervals AS (\n"
        "  SELECT\n"
        "    ROW_NUMBER() OVER (ORDER BY ts) AS id,\n"
        "    ts,\n"
        "    dur\n"
        "  FROM interval_merge_overlapping!(fti_intervals_raw, 0)\n"
        ")";
  } else {
    // Partitioned: use interval_merge_overlapping_partitioned for each
    // partition column and add a dummy id. We only support a single partition
    // column for the merge operation, but multiple for the intersection.
    // For simplicity, we merge on the first partition column.
    sql +=
        ",\nfti_intervals AS (\n"
        "  SELECT\n"
        "    ROW_NUMBER() OVER (ORDER BY ts) AS id,\n"
        "    ts,\n"
        "    dur";
    for (const auto& col : partition_cols) {
      sql += ",\n    " + col;
    }
    sql +=
        "\n  FROM "
        "interval_merge_overlapping_partitioned!(fti_intervals_raw, (" +
        partition_cols[0] + "))\n)";
  }

  // Use _interval_intersect! macro to compute overlaps
  sql += "\nSELECT ";

  // Build the column list based on clip_to_intervals and select_columns
  if (select_cols.empty()) {
    // No explicit column selection: use base_0.*
    if (clip_to_intervals) {
      // When clipping, select intersected ts/dur, then all base columns
      // This produces duplicate ts/dur columns, but SQL will use the first
      // occurrence
      sql += "ii.ts, ii.dur, base_0.*";
    } else {
      // When not clipping, just select all base columns as-is (no duplicates)
      sql += "base_0.*";
    }
  } else {
    // Explicit column selection
    bool first = true;

    if (clip_to_intervals) {
      // When clipping: ii.ts, ii.dur first, then other columns, then
      // original_ts/dur at end
      sql += "ii.ts, ii.dur";
      first = false;

      // Add non-ts/dur columns
      for (const auto& col : select_cols) {
        if (!base::CaseInsensitiveEqual(col, "ts") &&
            !base::CaseInsensitiveEqual(col, "dur")) {
          sql += ", base_0." + col;
        }
      }

      // Add original_ts and original_dur at the end if they were requested
      for (const auto& col : select_cols) {
        if (base::CaseInsensitiveEqual(col, "ts")) {
          sql += ", base_0.ts AS original_ts";
        } else if (base::CaseInsensitiveEqual(col, "dur")) {
          sql += ", base_0.dur AS original_dur";
        }
      }
    } else {
      // When not clipping: preserve exact order from select_cols
      for (const auto& col : select_cols) {
        if (!first)
          sql += ", ";
        sql += "base_0." + col;
        first = false;
      }
    }
  }

  sql += "\nFROM _interval_intersect!((fti_base, fti_intervals), (";
  for (size_t i = 0; i < partition_cols.size(); ++i) {
    if (i > 0) {
      sql += ", ";
    }
    sql += partition_cols[i];
  }
  sql += ")) ii\nJOIN fti_base AS base_0 ON ii.id_0 = base_0.id)";

  return sql;
}

base::StatusOr<std::string> GeneratorImpl::Join(
    const StructuredQuery::ExperimentalJoin::Decoder& join) {
  if (!join.has_left_query()) {
    return base::ErrStatus("Join must specify a left query");
  }
  if (!join.has_right_query()) {
    return base::ErrStatus("Join must specify a right query");
  }
  if (!join.has_equality_columns() && !join.has_freeform_condition()) {
    return base::ErrStatus(
        "Join must specify either equality_columns or freeform_condition");
  }

  std::string left_table = NestedSource(join.left_query());
  std::string right_table = NestedSource(join.right_query());

  std::string join_type_str;
  switch (join.type()) {
    case StructuredQuery::ExperimentalJoin::INNER:
      join_type_str = "INNER";
      break;
    case StructuredQuery::ExperimentalJoin::LEFT:
      join_type_str = "LEFT";
      break;
  }

  std::string condition;
  if (join.has_equality_columns()) {
    StructuredQuery::ExperimentalJoin::EqualityColumns::Decoder eq_cols(
        join.equality_columns());
    if (!eq_cols.has_left_column()) {
      return base::ErrStatus("EqualityColumns must specify a left column");
    }
    if (!eq_cols.has_right_column()) {
      return base::ErrStatus("EqualityColumns must specify a right column");
    }
    condition = left_table + "." + eq_cols.left_column().ToStdString() + " = " +
                right_table + "." + eq_cols.right_column().ToStdString();
  } else {
    StructuredQuery::ExperimentalJoin::FreeformCondition::Decoder free_cond(
        join.freeform_condition());
    if (!free_cond.has_left_query_alias()) {
      return base::ErrStatus(
          "FreeformCondition must specify a left query alias");
    }
    if (!free_cond.has_right_query_alias()) {
      return base::ErrStatus(
          "FreeformCondition must specify a right query alias");
    }
    if (!free_cond.has_sql_expression()) {
      return base::ErrStatus("FreeformCondition must specify a sql expression");
    }
    std::string left_alias = free_cond.left_query_alias().ToStdString();
    std::string right_alias = free_cond.right_query_alias().ToStdString();
    std::string sql_expr = free_cond.sql_expression().ToStdString();

    // Use aliases in the FROM clause
    condition = sql_expr;
    std::string sql = "(SELECT * FROM " + left_table + " AS " + left_alias +
                      " " + join_type_str + " JOIN " + right_table + " AS " +
                      right_alias + " ON " + condition + ")";
    return sql;
  }

  std::string sql = "(SELECT * FROM " + left_table + " " + join_type_str +
                    " JOIN " + right_table + " ON " + condition + ")";
  return sql;
}

// Helper function to validate that all queries in a UNION have matching columns
base::Status ValidateUnionColumns(
    const std::vector<std::vector<std::string>>& query_columns) {
  if (query_columns.empty() || query_columns[0].empty()) {
    return base::OkStatus();
  }

  const auto& reference_cols = query_columns[0];
  std::set<std::string> reference_set(reference_cols.begin(),
                                      reference_cols.end());

  for (size_t i = 1; i < query_columns.size(); ++i) {
    if (query_columns[i].empty()) {
      continue;
    }

    const auto& cols = query_columns[i];
    if (cols.size() != reference_cols.size()) {
      return base::ErrStatus(
          "Union queries have different column counts (query %zu vs query 0)",
          i);
    }

    std::set<std::string> cols_set(cols.begin(), cols.end());
    if (cols_set != reference_set) {
      return base::ErrStatus(
          "Union queries have different column sets (query %zu vs query 0)", i);
    }
  }

  return base::OkStatus();
}

base::StatusOr<std::string> GeneratorImpl::Union(
    const StructuredQuery::ExperimentalUnion::Decoder& union_decoder) {
  auto queries = union_decoder.queries();
  if (!queries) {
    return base::ErrStatus("Union must specify at least one query");
  }

  // Count the number of queries and collect column information for validation
  size_t query_count = 0;
  std::vector<std::vector<std::string>> query_columns;

  for (auto it = queries; it; ++it) {
    query_count++;
    StructuredQuery::Decoder query(*it);

    // Extract column names from select_columns if present
    std::vector<std::string> cols;
    if (auto select_cols = query.select_columns(); select_cols) {
      for (auto col_it = select_cols; col_it; ++col_it) {
        StructuredQuery::SelectColumn::Decoder column(*col_it);
        std::string col_name;

        // Use alias if present, otherwise use column name or expression
        if (column.has_alias()) {
          col_name = column.alias().ToStdString();
        } else if (column.has_column_name_or_expression()) {
          col_name = column.column_name_or_expression().ToStdString();
        } else if (column.has_column_name()) {
          col_name = column.column_name().ToStdString();
        }

        if (!col_name.empty()) {
          cols.push_back(col_name);
        }
      }
    }

    query_columns.push_back(cols);
  }

  if (query_count < 2) {
    return base::ErrStatus("Union must specify at least two queries");
  }

  // Validate that all queries have the same columns (if columns are specified)
  RETURN_IF_ERROR(ValidateUnionColumns(query_columns));

  // Build a local WITH clause to avoid CTE name conflicts with global scope.
  // Similar to IntervalIntersect, we create local CTEs with unique names.
  std::string sql = "(\n  WITH ";
  size_t idx = 0;
  for (auto it = union_decoder.queries(); it; ++it, ++idx) {
    if (idx > 0) {
      sql += ", ";
    }
    sql += "union_query_" + std::to_string(idx) + " AS (\n  ";
    sql += "SELECT *\n  ";
    sql += "FROM " + NestedSource(*it) + ")";
  }

  // Build the UNION/UNION ALL query
  std::string union_keyword =
      union_decoder.use_union_all() ? "UNION ALL" : "UNION";
  sql += "\n  SELECT *\n  FROM union_query_0";
  for (size_t i = 1; i < query_count; ++i) {
    sql += "\n  " + union_keyword + "\n  SELECT *\n  FROM union_query_" +
           std::to_string(i);
  }
  sql += ")";

  return sql;
}

base::StatusOr<std::string> GeneratorImpl::AddColumns(
    const StructuredQuery::ExperimentalAddColumns::Decoder& add_columns) {
  // Validate required fields
  if (!add_columns.has_core_query()) {
    return base::ErrStatus("AddColumns must specify a core query");
  }
  if (!add_columns.has_input_query()) {
    return base::ErrStatus("AddColumns must specify an input query");
  }
  if (!add_columns.has_equality_columns() &&
      !add_columns.has_freeform_condition()) {
    return base::ErrStatus(
        "AddColumns must specify either equality_columns or "
        "freeform_condition");
  }

  // Validate input_columns
  auto input_columns = add_columns.input_columns();
  if (!input_columns) {
    return base::ErrStatus("AddColumns must specify at least one input column");
  }
  size_t column_count = 0;
  for (auto it = input_columns; it; ++it) {
    column_count++;
  }
  if (column_count == 0) {
    return base::ErrStatus("AddColumns must specify at least one input column");
  }

  // Generate nested sources
  std::string core_table = NestedSource(add_columns.core_query());
  std::string input_table = NestedSource(add_columns.input_query());

  // Build the SELECT clause with all core columns plus input columns
  std::string select_clause = "core.*";
  for (auto it = add_columns.input_columns(); it; ++it) {
    StructuredQuery::SelectColumn::Decoder col_decoder(*it);

    // Get the column name or expression
    if (!col_decoder.has_column_name_or_expression()) {
      return base::ErrStatus(
          "SelectColumn must specify column_name_or_expression");
    }
    std::string col_expr =
        col_decoder.column_name_or_expression().ToStdString();
    if (col_expr.empty()) {
      return base::ErrStatus("Input column name cannot be empty");
    }

    // Add the column with optional alias
    select_clause += ", input." + col_expr;
    if (col_decoder.has_alias()) {
      std::string alias = col_decoder.alias().ToStdString();
      if (!alias.empty()) {
        select_clause += " AS " + alias;
      }
    }
  }

  // Build the join condition
  std::string condition;
  if (add_columns.has_equality_columns()) {
    StructuredQuery::ExperimentalJoin::EqualityColumns::Decoder eq_cols(
        add_columns.equality_columns());
    if (!eq_cols.has_left_column()) {
      return base::ErrStatus("EqualityColumns must specify a left column");
    }
    if (!eq_cols.has_right_column()) {
      return base::ErrStatus("EqualityColumns must specify a right column");
    }
    condition = "core." + eq_cols.left_column().ToStdString() + " = input." +
                eq_cols.right_column().ToStdString();
  } else {
    StructuredQuery::ExperimentalJoin::FreeformCondition::Decoder free_cond(
        add_columns.freeform_condition());
    if (!free_cond.has_left_query_alias()) {
      return base::ErrStatus(
          "FreeformCondition must specify a left query alias");
    }
    if (!free_cond.has_right_query_alias()) {
      return base::ErrStatus(
          "FreeformCondition must specify a right query alias");
    }
    if (!free_cond.has_sql_expression()) {
      return base::ErrStatus("FreeformCondition must specify a sql expression");
    }

    std::string left_alias = free_cond.left_query_alias().ToStdString();
    std::string right_alias = free_cond.right_query_alias().ToStdString();

    // Validate that aliases match "core" and "input"
    if (left_alias != "core") {
      return base::ErrStatus(
          "FreeformCondition left_query_alias must be 'core', got '%s'",
          left_alias.c_str());
    }
    if (right_alias != "input") {
      return base::ErrStatus(
          "FreeformCondition right_query_alias must be 'input', got '%s'",
          right_alias.c_str());
    }

    condition = free_cond.sql_expression().ToStdString();
  }

  // Generate the final SQL using LEFT JOIN to keep all core rows
  std::string sql = "(SELECT " + select_clause + " FROM " + core_table +
                    " AS core LEFT JOIN " + input_table + " AS input ON " +
                    condition + ")";

  return sql;
}

base::StatusOr<std::string> GeneratorImpl::CreateSlices(
    const StructuredQuery::ExperimentalCreateSlices::Decoder& create_slices) {
  // Validate required fields
  if (!create_slices.has_starts_query()) {
    return base::ErrStatus("CreateSlices must specify a starts_query");
  }
  if (!create_slices.has_ends_query()) {
    return base::ErrStatus("CreateSlices must specify an ends_query");
  }

  // Default to "ts" if not specified or empty
  std::string starts_ts_col =
      create_slices.has_starts_ts_column()
          ? create_slices.starts_ts_column().ToStdString()
          : "ts";
  std::string ends_ts_col = create_slices.has_ends_ts_column()
                                ? create_slices.ends_ts_column().ToStdString()
                                : "ts";

  // If explicitly set to empty string, also default to "ts"
  if (starts_ts_col.empty()) {
    starts_ts_col = "ts";
  }
  if (ends_ts_col.empty()) {
    ends_ts_col = "ts";
  }

  // Generate nested sources
  std::string starts_table = NestedSource(create_slices.starts_query());
  std::string ends_table = NestedSource(create_slices.ends_query());

  // Reference the intervals.create_intervals module which contains
  // _interval_create
  referenced_modules_.Insert("intervals.create_intervals", nullptr);

  // Use _interval_create! macro which delegates to
  // __intrinsic_interval_create, an O(n+m) two-pointer C++ implementation.
  // The macro expects inputs with a `ts` column, so we rename if needed.
  return base::StackString<1024>(
             "(SELECT * FROM _interval_create!("
             "(SELECT %s AS ts FROM %s), "
             "(SELECT %s AS ts FROM %s)))",
             starts_ts_col.c_str(), starts_table.c_str(), ends_ts_col.c_str(),
             ends_table.c_str())
      .ToStdString();
}

base::StatusOr<std::string> GeneratorImpl::CounterIntervals(
    const StructuredQuery::ExperimentalCounterIntervals::Decoder&
        counter_intervals) {
  // Validate required fields
  if (!counter_intervals.has_input_query()) {
    return base::ErrStatus("CounterIntervals must specify an input_query");
  }

  // Reference the counters.intervals module which contains
  // counter_leading_intervals
  referenced_modules_.Insert("counters.intervals", nullptr);

  // Generate nested source
  std::string input_table = NestedSource(counter_intervals.input_query());

  // Use counter_leading_intervals! macro to convert counter data to intervals
  // The macro expects a table with (id, ts, track_id, value) columns
  // and returns (id, ts, dur, track_id, value, next_value, delta_value)
  std::string sql =
      "(SELECT * FROM counter_leading_intervals!(" + input_table + "))";

  return sql;
}

base::StatusOr<std::string> GeneratorImpl::FilterIn(
    const StructuredQuery::ExperimentalFilterIn::Decoder& filter_in) {
  // Validate required fields
  if (filter_in.base().size == 0) {
    return base::ErrStatus("FilterIn must specify a base query");
  }
  if (filter_in.match_values().size == 0) {
    return base::ErrStatus("FilterIn must specify a match_values query");
  }
  if (filter_in.base_column().size == 0) {
    return base::ErrStatus("FilterIn must specify a base_column");
  }
  if (filter_in.match_column().size == 0) {
    return base::ErrStatus("FilterIn must specify a match_column");
  }

  std::string base_col = filter_in.base_column().ToStdString();
  std::string match_col = filter_in.match_column().ToStdString();

  // Generate nested sources
  std::string base_source = NestedSource(filter_in.base());
  std::string match_source = NestedSource(filter_in.match_values());

  // Build the SQL for the filter-in operation (semi-join)
  std::string sql = "(SELECT base.* FROM " + base_source + " AS base WHERE " +
                    "base." + base_col + " IN (SELECT " + match_col + " FROM " +
                    match_source + "))";

  return sql;
}

base::StatusOr<std::string> GeneratorImpl::ReferencedSharedQuery(
    protozero::ConstChars raw_id) {
  std::string id = raw_id.ToStdString();
  for (std::optional<size_t> curr_idx = state_index_; curr_idx;
       curr_idx = state_[*curr_idx].parent_index) {
    const auto& query = state_[*curr_idx];
    if (query.id_from_proto && *query.id_from_proto == id) {
      return base::ErrStatus(
          "Cycle detected in structured query dependencies involving query "
          "with "
          "id '%s'",
          id.c_str());
    }
  }
  auto* it = query_protos_.Find(id);
  if (!it) {
    return base::ErrStatus("Shared query with id '%s' not found", id.c_str());
  }
  // Check if this query is already in queries_ (non-inlined case).
  auto sq = std::find_if(queries_.begin(), queries_.end(),
                         [&](const Query& sq) { return id == sq.id; });
  if (sq != queries_.end()) {
    return sq->table_name;
  }
  // Check if we've already created a state entry for this ID (inlined case).
  // This prevents creating duplicate CTEs with collision suffixes when the
  // same shared query is referenced multiple times.
  for (const auto& s : state_) {
    if (s.type == QueryType::kShared && s.id_from_proto &&
        *s.id_from_proto == id) {
      return s.table_name;
    }
  }
  state_.emplace_back(QueryType::kShared,
                      protozero::ConstBytes{it->data.get(), it->size},
                      state_.size(), state_index_, used_table_names_);
  return state_.back().table_name;
}

std::string GeneratorImpl::NestedSource(protozero::ConstBytes bytes) {
  state_.emplace_back(QueryType::kNested, bytes, state_.size(), state_index_,
                      used_table_names_);
  return state_.back().table_name;
}

base::StatusOr<std::string> GeneratorImpl::SingleFilter(
    const StructuredQuery::Filter::Decoder& filter) {
  std::string column_name = filter.column_name().ToStdString();
  auto op = static_cast<StructuredQuery::Filter::Operator>(filter.op());
  ASSIGN_OR_RETURN(std::string op_str, OperatorToString(op));

  if (op == StructuredQuery::Filter::Operator::IS_NULL ||
      op == StructuredQuery::Filter::Operator::IS_NOT_NULL) {
    return column_name + " " + op_str;
  }

  std::string sql = column_name + " " + op_str + " ";

  if (auto srhs = filter.string_rhs(); srhs) {
    sql += "'" + (*srhs++).ToStdString() + "'";
    for (; srhs; ++srhs) {
      sql += " OR " + column_name + " " + op_str + " '" +
             (*srhs).ToStdString() + "'";
    }
  } else if (auto drhs = filter.double_rhs(); drhs) {
    sql += std::to_string((*drhs++));
    for (; drhs; ++drhs) {
      sql += " OR " + column_name + " " + op_str + " " + std::to_string(*drhs);
    }
  } else if (auto irhs = filter.int64_rhs(); irhs) {
    sql += std::to_string(*irhs++);
    for (; irhs; ++irhs) {
      sql += " OR " + column_name + " " + op_str + " " + std::to_string(*irhs);
    }
  } else {
    return base::ErrStatus("Filter must specify a right-hand side");
  }
  return sql;
}

base::StatusOr<std::string> GeneratorImpl::Filters(
    protozero::RepeatedFieldIterator<protozero::ConstBytes> filters) {
  std::string sql;
  for (auto it = filters; it; ++it) {
    StructuredQuery::Filter::Decoder filter(*it);
    if (!sql.empty()) {
      sql += " AND ";
    }
    ASSIGN_OR_RETURN(std::string filter_sql, SingleFilter(filter));
    sql += filter_sql;
  }
  return sql;
}

base::StatusOr<std::string> GeneratorImpl::ExperimentalFilterGroup(
    const StructuredQuery::ExperimentalFilterGroup::Decoder& exp_filter_group) {
  auto op = static_cast<StructuredQuery::ExperimentalFilterGroup::Operator>(
      exp_filter_group.op());
  if (op == StructuredQuery::ExperimentalFilterGroup::UNSPECIFIED) {
    return base::ErrStatus(
        "ExperimentalFilterGroup must specify an operator (AND or OR)");
  }

  std::string op_str;
  switch (op) {
    case StructuredQuery::ExperimentalFilterGroup::AND:
      op_str = " AND ";
      break;
    case StructuredQuery::ExperimentalFilterGroup::OR:
      op_str = " OR ";
      break;
    case StructuredQuery::ExperimentalFilterGroup::UNSPECIFIED:
      return base::ErrStatus(
          "ExperimentalFilterGroup operator cannot be UNSPECIFIED");
  }

  std::string sql;
  size_t item_count = 0;

  // Process simple filters
  for (auto it = exp_filter_group.filters(); it; ++it) {
    StructuredQuery::Filter::Decoder filter(*it);
    if (item_count > 0) {
      sql += op_str;
    }
    ASSIGN_OR_RETURN(std::string filter_sql, SingleFilter(filter));
    sql += filter_sql;
    item_count++;
  }

  // Process nested groups (wrap in parentheses)
  for (auto it = exp_filter_group.groups(); it; ++it) {
    StructuredQuery::ExperimentalFilterGroup::Decoder group(*it);
    if (item_count > 0) {
      sql += op_str;
    }
    ASSIGN_OR_RETURN(std::string group_sql, ExperimentalFilterGroup(group));
    sql += "(" + group_sql + ")";
    item_count++;
  }

  // Process SQL expressions
  for (auto it = exp_filter_group.sql_expressions(); it; ++it) {
    if (item_count > 0) {
      sql += op_str;
    }
    sql += (*it).ToStdString();
    item_count++;
  }

  if (item_count == 0) {
    return base::ErrStatus(
        "ExperimentalFilterGroup must have at least one filter, group, or SQL "
        "expression");
  }

  return sql;
}

base::StatusOr<std::string> GeneratorImpl::GroupBy(
    protozero::RepeatedFieldIterator<protozero::ConstChars> group_by) {
  std::string sql;
  for (auto it = group_by; it; ++it) {
    if (sql.empty()) {
      sql += "GROUP BY ";
    } else {
      sql += ", ";
    }
    sql += (*it).ToStdString();
  }
  return sql;
}

base::StatusOr<std::string> GeneratorImpl::OrderBy(
    const StructuredQuery::OrderBy::Decoder& order_by) {
  auto specs = order_by.ordering_specs();
  if (!specs) {
    return base::ErrStatus("ORDER BY must specify at least one ordering spec");
  }

  // The order of ordering_specs is significant: the first spec is the primary
  // sort key, subsequent specs are used to break ties.
  // See SQL-92 standard section 7.10 (Sort specification list).
  std::string sql = "ORDER BY ";
  bool first = true;
  for (auto it = specs; it; ++it) {
    StructuredQuery::OrderBy::OrderingSpec::Decoder spec(*it);
    if (!first) {
      sql += ", ";
    }
    first = false;

    if (spec.column_name().size == 0) {
      return base::ErrStatus("ORDER BY column_name cannot be empty");
    }
    sql += spec.column_name().ToStdString();

    if (spec.has_direction()) {
      switch (spec.direction()) {
        case StructuredQuery::OrderBy::ASC:
          sql += " ASC";
          break;
        case StructuredQuery::OrderBy::DESC:
          sql += " DESC";
          break;
        case StructuredQuery::OrderBy::UNSPECIFIED:
          // Default to ASC, no need to add anything
          break;
      }
    }
  }
  return sql;
}

base::StatusOr<std::string> GeneratorImpl::SelectColumnsAggregates(
    protozero::RepeatedFieldIterator<protozero::ConstChars> group_by_cols,
    protozero::RepeatedFieldIterator<protozero::ConstBytes> aggregates,
    protozero::RepeatedFieldIterator<protozero::ConstBytes> select_cols) {
  base::FlatHashMap<std::string, std::optional<std::string>> output;
  if (select_cols) {
    for (auto it = select_cols; it; ++it) {
      StructuredQuery::SelectColumn::Decoder select(*it);
      std::string selected_col_name;
      if (select.has_column_name_or_expression()) {
        selected_col_name = select.column_name_or_expression().ToStdString();
      } else {
        selected_col_name = select.column_name().ToStdString();
      }
      output.Insert(selected_col_name,
                    select.has_alias()
                        ? std::make_optional(select.alias().ToStdString())
                        : std::nullopt);
    }
  } else {
    for (auto it = group_by_cols; it; ++it) {
      output.Insert((*it).ToStdString(), std::nullopt);
    }
    for (auto it = aggregates; it; ++it) {
      StructuredQuery::GroupBy::Aggregate::Decoder aggregate(*it);
      output.Insert(aggregate.result_column_name().ToStdString(), std::nullopt);
    }
  }

  std::string sql;
  auto itg = group_by_cols;
  for (; itg; ++itg) {
    std::string column_name = (*itg).ToStdString();
    auto* o = output.Find(column_name);
    if (!o) {
      continue;
    }
    if (!sql.empty()) {
      sql += ", ";
    }
    if (o->has_value()) {
      sql += column_name + " AS " + o->value();
    } else {
      sql += column_name;
    }
  }

  for (auto ita = aggregates; ita; ++ita) {
    StructuredQuery::GroupBy::Aggregate::Decoder aggregate(*ita);
    std::string res_column_name = aggregate.result_column_name().ToStdString();
    auto* o = output.Find(res_column_name);
    if (!o) {
      continue;
    }
    if (!sql.empty()) {
      sql += ", ";
    }
    ASSIGN_OR_RETURN(std::string agg, AggregateToString(aggregate));
    if (o->has_value()) {
      sql += agg + " AS " + o->value();
    } else {
      sql += agg + " AS " + res_column_name;
    }
  }
  return sql;
}

base::StatusOr<std::string> GeneratorImpl::SelectColumnsNoAggregates(
    protozero::RepeatedFieldIterator<protozero::ConstBytes> select_columns) {
  if (!select_columns) {
    return std::string("*");
  }
  std::string sql;
  for (auto it = select_columns; it; ++it) {
    StructuredQuery::SelectColumn::Decoder column(*it);
    if (!sql.empty()) {
      sql += ", ";
    }
    std::string col_expr;
    if (column.has_column_name_or_expression()) {
      col_expr = column.column_name_or_expression().ToStdString();
    } else {
      col_expr = column.column_name().ToStdString();
    }

    if (column.has_alias()) {
      sql += col_expr + " AS " + column.alias().ToStdString();
    } else {
      sql += col_expr;
    }
  }
  return sql;
}

base::StatusOr<std::string> GeneratorImpl::OperatorToString(
    StructuredQuery::Filter::Operator op) {
  switch (op) {
    case StructuredQuery::Filter::EQUAL:
      return std::string("=");
    case StructuredQuery::Filter::NOT_EQUAL:
      return std::string("!=");
    case StructuredQuery::Filter::LESS_THAN:
      return std::string("<");
    case StructuredQuery::Filter::LESS_THAN_EQUAL:
      return std::string("<=");
    case StructuredQuery::Filter::GREATER_THAN:
      return std::string(">");
    case StructuredQuery::Filter::GREATER_THAN_EQUAL:
      return std::string(">=");
    case StructuredQuery::Filter::GLOB:
      return std::string("GLOB");
    case StructuredQuery::Filter::IS_NULL:
      return std::string("IS NULL");
    case StructuredQuery::Filter::IS_NOT_NULL:
      return std::string("IS NOT NULL");
    case StructuredQuery::Filter::UNKNOWN:
      return base::ErrStatus("Invalid filter operator %d", op);
  }
  PERFETTO_FATAL("For GCC");
}

base::StatusOr<std::string> GeneratorImpl::AggregateToString(
    const StructuredQuery::GroupBy::Aggregate::Decoder& aggregate) {
  auto op =
      static_cast<StructuredQuery::GroupBy::Aggregate::Op>(aggregate.op());

  if (op == StructuredQuery::GroupBy::Aggregate::COUNT &&
      !aggregate.has_column_name()) {
    return std::string("COUNT(*)");
  }

  if (op == StructuredQuery::GroupBy::Aggregate::CUSTOM) {
    if (!aggregate.has_custom_sql_expression()) {
      return base::ErrStatus(
          "Custom SQL expression not specified for CUSTOM aggregation");
    }
    return aggregate.custom_sql_expression().ToStdString();
  }

  if (!aggregate.has_column_name()) {
    return base::ErrStatus("Column name not specified for aggregation");
  }
  std::string column_name = aggregate.column_name().ToStdString();

  switch (op) {
    case StructuredQuery::GroupBy::Aggregate::COUNT:
      return "COUNT(" + column_name + ")";
    case StructuredQuery::GroupBy::Aggregate::COUNT_DISTINCT:
      return "COUNT(DISTINCT " + column_name + ")";
    case StructuredQuery::GroupBy::Aggregate::SUM:
      return "SUM(" + column_name + ")";
    case StructuredQuery::GroupBy::Aggregate::MIN:
      return "MIN(" + column_name + ")";
    case StructuredQuery::GroupBy::Aggregate::MAX:
      return "MAX(" + column_name + ")";
    case StructuredQuery::GroupBy::Aggregate::MEAN:
      return "AVG(" + column_name + ")";
    case StructuredQuery::GroupBy::Aggregate::MEDIAN:
      return "PERCENTILE(" + column_name + ", 50)";
    case StructuredQuery::GroupBy::Aggregate::PERCENTILE:
      if (!aggregate.has_percentile()) {
        return base::ErrStatus("Percentile not specified for aggregation");
      }
      return "PERCENTILE(" + column_name + ", " +
             std::to_string(aggregate.percentile()) + ")";
    case StructuredQuery::GroupBy::Aggregate::DURATION_WEIGHTED_MEAN:
      return "SUM(cast_double!(" + column_name +
             " * dur)) / cast_double!(SUM(dur))";
    case StructuredQuery::GroupBy::Aggregate::CUSTOM:
      PERFETTO_FATAL("CUSTOM aggregation should have been handled above");
    case StructuredQuery::GroupBy::Aggregate::UNSPECIFIED:
      return base::ErrStatus("Invalid aggregate operator %d", op);
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace

base::StatusOr<std::string> StructuredQueryGenerator::Generate(
    const uint8_t* data,
    size_t size,
    bool inline_shared_queries) {
  GeneratorImpl impl(query_protos_, referenced_queries_, referenced_modules_,
                     preambles_);
  ASSIGN_OR_RETURN(
      std::string sql,
      impl.Generate(protozero::ConstBytes{data, size}, inline_shared_queries));
  return sql;
}

base::StatusOr<std::string> StructuredQueryGenerator::GenerateById(
    const std::string& id,
    bool inline_shared_queries) {
  auto* ptr = query_protos_.Find(id);
  if (!ptr) {
    return base::ErrStatus("Query with id %s not found", id.c_str());
  }
  return Generate(ptr->data.get(), ptr->size, inline_shared_queries);
}

base::StatusOr<std::string> StructuredQueryGenerator::AddQuery(
    const uint8_t* data,
    size_t size) {
  StructuredQuery::Decoder decoder(data, size);
  if (!decoder.has_id()) {
    return base::ErrStatus(
        "Unable to find id for shared query: all shared queries must have an "
        "id specified");
  }
  std::string id = decoder.id().ToStdString();

  // Extract module references so ComputeReferencedModules() returns them
  // before Generate() is called. This ensures PrepareGenerator can include
  // modules before materialization.
  for (auto it = decoder.referenced_modules(); it; ++it) {
    referenced_modules_.Insert(it->as_std_string(), nullptr);
  }
  if (decoder.has_table()) {
    StructuredQuery::Table::Decoder table(decoder.table());
    if (table.module_name().size > 0) {
      referenced_modules_.Insert(table.module_name().ToStdString(), nullptr);
    }
  }

  auto ptr = std::make_unique<uint8_t[]>(size);
  memcpy(ptr.get(), data, size);
  auto [it, inserted] =
      query_protos_.Insert(id, QueryProto{std::move(ptr), size});
  if (!inserted) {
    return base::ErrStatus("Multiple shared queries specified with the ids %s",
                           id.c_str());
  }
  return id;
}

std::vector<std::string> StructuredQueryGenerator::ComputeReferencedModules()
    const {
  std::vector<std::string> modules;
  for (auto it = referenced_modules_.GetIterator(); it; ++it) {
    modules.emplace_back(it.key());
  }
  return modules;
}

}  // namespace perfetto::trace_processor::perfetto_sql::generator
