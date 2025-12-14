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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
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
struct QueryState {
  QueryState(QueryType _type,
             protozero::ConstBytes _bytes,
             size_t index,
             std::optional<size_t> parent_idx)
      : type(_type), bytes(_bytes), parent_index(parent_idx) {
    protozero::ProtoDecoder decoder(bytes);
    std::string prefix = type == QueryType::kShared ? "shared_sq_" : "sq_";
    if (auto id = decoder.FindField(StructuredQuery::kIdFieldNumber); id) {
      id_from_proto = id.as_std_string();
      table_name = prefix + *id_from_proto;
    } else {
      table_name = prefix + std::to_string(index);
    }
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

  base::StatusOr<std::string> Generate(protozero::ConstBytes);

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

  // Nested sources
  std::string NestedSource(protozero::ConstBytes);
  base::StatusOr<std::string> ReferencedSharedQuery(
      protozero::ConstChars raw_id);

  base::StatusOr<std::string> IntervalIntersect(
      const StructuredQuery::IntervalIntersect::Decoder&);

  // Filtering.
  static base::StatusOr<std::string> Filters(RepeatedProto filters);

  // Aggregation.
  static base::StatusOr<std::string> GroupBy(RepeatedString group_by);
  static base::StatusOr<std::string> SelectColumnsAggregates(
      RepeatedString group_by,
      RepeatedProto aggregates,
      RepeatedProto select_cols);
  base::StatusOr<std::string> SelectColumnsNoAggregates(
      RepeatedProto select_columns);

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
};

base::StatusOr<std::string> GeneratorImpl::Generate(
    protozero::ConstBytes bytes) {
  state_.emplace_back(QueryType::kRoot, bytes, state_.size(), std::nullopt);
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
  std::string sql = "WITH ";
  for (size_t i = 0; i < state_.size(); ++i) {
    QueryState& state = state_[state_.size() - i - 1];
    if (state.type == QueryType::kShared) {
      queries_.emplace_back(
          Query{state.id_from_proto.value(), state.table_name, state.sql});
      continue;
    }
    sql += state.table_name + " AS (" + state.sql + ")";
    if (i < state_.size() - 1) {
      sql += ", ";
    }
  }
  sql += " SELECT * FROM " + state_[0].table_name;
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
    } else if (q.has_simple_slices()) {
      StructuredQuery::SimpleSlices::Decoder slices(q.simple_slices());
      ASSIGN_OR_RETURN(source, SimpleSlices(slices));
    } else if (q.has_interval_intersect()) {
      StructuredQuery::IntervalIntersect::Decoder ii(q.interval_intersect());
      ASSIGN_OR_RETURN(source, IntervalIntersect(ii));
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

  ASSIGN_OR_RETURN(std::string filters, Filters(q.filters()));

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

  std::string sql = "SELECT " + select + " FROM " + source;
  if (!filters.empty()) {
    sql += " WHERE " + filters;
  }
  if (!group_by.empty()) {
    sql += " " + group_by;
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
    preambles_.push_back(sql.preamble().ToStdString());
    final_sql_statement = source_sql;
  } else {
    auto [parsed_preamble, main_sql] = GetPreambleAndSql(source_sql.sql());
    if (!parsed_preamble.sql().empty()) {
      preambles_.push_back(parsed_preamble.sql());
    } else if (sql.has_preamble()) {
      return base::ErrStatus(
          "Sql source specifies both `preamble` and has multiple statements in "
          "the `sql` field. This is not supported - plase don't use `preamble` "
          "and pass all the SQL you want to execute in the `sql` field.");
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

  std::string generated_sql = "(SELECT " + cols_str + " FROM (" +
                              std::move(rewriter).Build().sql() + "))";
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

  std::string sql =
      "(WITH iibase AS (SELECT * FROM " + NestedSource(interval.base()) + ")";
  auto ii = interval.interval_intersect();
  for (size_t i = 0; ii; ++ii, ++i) {
    sql += ", iisource" + std::to_string(i) + " AS (SELECT * FROM " +
           NestedSource(*ii) + ") ";
  }

  sql += "SELECT ii.ts, ii.dur, iibase.*";
  ii = interval.interval_intersect();
  for (size_t i = 0; ii; ++ii) {
    sql += ", iisource" + std::to_string(i) + ".*";
  }
  sql += " FROM _interval_intersect!((iibase";
  ii = interval.interval_intersect();
  for (size_t i = 0; ii; ++ii) {
    sql += ", iisource" + std::to_string(i);
  }
  sql += "), ()) ii JOIN iibase ON ii.id_0 = iibase.id";

  ii = interval.interval_intersect();
  for (size_t i = 0; ii; ++ii) {
    sql += " JOIN iisource" + std::to_string(i) + " ON ii.id_" +
           std::to_string(i + 1) + " = iisource" + std::to_string(i) + ".id";
  }
  sql += ")";
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
  auto sq = std::find_if(queries_.begin(), queries_.end(),
                         [&](const Query& sq) { return id == sq.id; });
  if (sq != queries_.end()) {
    return sq->table_name;
  }
  state_.emplace_back(QueryType::kShared,
                      protozero::ConstBytes{it->data.get(), it->size},
                      state_.size(), state_index_);
  return state_.back().table_name;
}

std::string GeneratorImpl::NestedSource(protozero::ConstBytes bytes) {
  state_.emplace_back(QueryType::kNested, bytes, state_.size(), state_index_);
  return state_.back().table_name;
}

base::StatusOr<std::string> GeneratorImpl::Filters(
    protozero::RepeatedFieldIterator<protozero::ConstBytes> filters) {
  std::string sql;
  for (auto it = filters; it; ++it) {
    StructuredQuery::Filter::Decoder filter(*it);
    if (!sql.empty()) {
      sql += " AND ";
    }

    std::string column_name = filter.column_name().ToStdString();
    auto op = static_cast<StructuredQuery::Filter::Operator>(filter.op());
    ASSIGN_OR_RETURN(std::string op_str, OperatorToString(op));

    if (op == StructuredQuery::Filter::Operator::IS_NULL ||
        op == StructuredQuery::Filter::Operator::IS_NOT_NULL) {
      sql += column_name + " " + op_str;
      continue;
    }

    sql += column_name + " " + op_str + " ";

    if (auto srhs = filter.string_rhs(); srhs) {
      sql += "'" + (*srhs++).ToStdString() + "'";
      for (; srhs; ++srhs) {
        sql += " OR " + column_name + " " + op_str + " '" +
               (*srhs).ToStdString() + "'";
      }
    } else if (auto drhs = filter.double_rhs(); drhs) {
      sql += std::to_string((*drhs++));
      for (; drhs; ++drhs) {
        sql +=
            " OR " + column_name + " " + op_str + " " + std::to_string(*drhs);
      }
    } else if (auto irhs = filter.int64_rhs(); irhs) {
      sql += std::to_string(*irhs++);
      for (; irhs; ++irhs) {
        sql +=
            " OR " + column_name + " " + op_str + " " + std::to_string(*irhs);
      }
    } else {
      return base::ErrStatus("Filter must specify a right-hand side");
    }
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

  if (!aggregate.has_column_name()) {
    return base::ErrStatus("Column name not specified for aggregation");
  }
  std::string column_name = aggregate.column_name().ToStdString();

  switch (op) {
    case StructuredQuery::GroupBy::Aggregate::COUNT:
      return "COUNT(" + column_name + ")";
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
    case StructuredQuery::GroupBy::Aggregate::UNSPECIFIED:
      return base::ErrStatus("Invalid aggregate operator %d", op);
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace

base::StatusOr<std::string> StructuredQueryGenerator::Generate(
    const uint8_t* data,
    size_t size) {
  GeneratorImpl impl(query_protos_, referenced_queries_, referenced_modules_,
                     preambles_);
  ASSIGN_OR_RETURN(std::string sql,
                   impl.Generate(protozero::ConstBytes{data, size}));
  return sql;
}

base::StatusOr<std::string> StructuredQueryGenerator::GenerateById(
    const std::string& id) {
  auto* ptr = query_protos_.Find(id);
  if (!ptr) {
    return base::ErrStatus("Query with id %s not found", id.c_str());
  }
  return Generate(ptr->data.get(), ptr->size);
}

base::Status StructuredQueryGenerator::AddQuery(const uint8_t* data,
                                                size_t size) {
  protozero::ProtoDecoder decoder(data, size);
  auto field = decoder.FindField(
      protos::pbzero::PerfettoSqlStructuredQuery::kIdFieldNumber);
  if (!field) {
    return base::ErrStatus(
        "Unable to find id for shared query: all shared queries must have an "
        "id specified");
  }
  std::string id = field.as_std_string();
  auto ptr = std::make_unique<uint8_t[]>(size);
  memcpy(ptr.get(), data, size);
  auto [it, inserted] =
      query_protos_.Insert(id, QueryProto{std::move(ptr), size});
  if (!inserted) {
    return base::ErrStatus("Multiple shared queries specified with the ids %s",
                           id.c_str());
  }
  return base::OkStatus();
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
