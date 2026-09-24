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

#include "src/trace_processor/shell/query.h"

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/trace_processor.h"

namespace perfetto::trace_processor {

base::StatusOr<QueryResult> ExtractQueryResult(Iterator* it, bool has_more) {
  QueryResult result;

  for (uint32_t c = 0; c < it->ColumnCount(); c++) {
    result.column_names.push_back(it->GetColumnName(c));
  }

  for (; has_more; has_more = it->Next()) {
    std::vector<std::string> row;
    for (uint32_t c = 0; c < it->ColumnCount(); c++) {
      SqlValue value = it->Get(c);
      std::string str_value;
      switch (value.type) {
        case SqlValue::Type::kNull:
          str_value = "\"[NULL]\"";
          break;
        case SqlValue::Type::kDouble:
          str_value =
              base::StackString<256>("%f", value.double_value).ToStdString();
          break;
        case SqlValue::Type::kLong:
          str_value = base::StackString<256>("%" PRIi64, value.long_value)
                          .ToStdString();
          break;
        case SqlValue::Type::kString:
          str_value = '"' + std::string(value.string_value) + '"';
          break;
        case SqlValue::Type::kBytes:
          str_value = "\"<raw bytes>\"";
          break;
      }

      row.push_back(std::move(str_value));
    }
    result.rows.push_back(std::move(row));
  }
  RETURN_IF_ERROR(it->Status());
  return result;
}

void PrintQueryResultAsCsv(const QueryResult& result, FILE* output) {
  for (uint32_t c = 0; c < result.column_names.size(); c++) {
    if (c > 0)
      fprintf(output, ",");
    fprintf(output, "\"%s\"", result.column_names[c].c_str());
  }
  fprintf(output, "\n");

  for (const auto& row : result.rows) {
    for (uint32_t c = 0; c < result.column_names.size(); c++) {
      if (c > 0)
        fprintf(output, ",");
      fprintf(output, "%s", row[c].c_str());
    }
    fprintf(output, "\n");
  }
}

base::Status RunQueriesWithoutOutput(TraceProcessor* trace_processor,
                                     const std::string& sql_query) {
  auto it = trace_processor->ExecuteQuery(sql_query);
  if (it.StatementWithOutputCount() > 0)
    return base::ErrStatus("Unexpected result from a query.");

  RETURN_IF_ERROR(it.Status());
  return it.Next() ? base::ErrStatus("Unexpected result from a query.")
                   : it.Status();
}

base::Status RunQueriesAndPrintResult(TraceProcessor* trace_processor,
                                      const std::string& sql_query,
                                      FILE* output) {
  PERFETTO_DLOG("Executing query: %s", sql_query.c_str());

  // Statements are executed one at a time and every statement's result set
  // is printed as CSV, with consecutive result sets separated by a single
  // blank line. Since our CSV writer quotes all strings, a blank line is
  // unambiguously a boundary between result sets. Statements with no output
  // print nothing, matching the sqlite3/duckdb shells.
  std::chrono::nanoseconds exec_dur{0};
  uint32_t offset = 0;
  bool executed_any_statement = false;
  bool printed_any_result = false;
  for (;;) {
    auto query_start = std::chrono::steady_clock::now();
    std::optional<Iterator> it =
        trace_processor->ExecuteNextStatement(sql_query, &offset);
    if (!it.has_value()) {
      break;
    }
    RETURN_IF_ERROR(it->Status());
    executed_any_statement = true;

    bool has_more = it->Next();
    RETURN_IF_ERROR(it->Status());

    // Statements without a result set (e.g. CREATE TABLE) print nothing.
    if (it->ColumnCount() == 0) {
      PERFETTO_DCHECK(!has_more);
      exec_dur += std::chrono::steady_clock::now() - query_start;
      continue;
    }

    // Statements with rows which nonetheless count as having no output are
    // those whose output is explicitly ignored (a single column named
    // `suppress_query_output`, void functions): step through them for their
    // side effects but print nothing.
    if (has_more && it->StatementWithOutputCount() == 0) {
      for (; has_more; has_more = it->Next()) {
      }
      RETURN_IF_ERROR(it->Status());
      exec_dur += std::chrono::steady_clock::now() - query_start;
      continue;
    }

    // A zero-row result set still prints its header, with one exception: the
    // `suppress_query_output` escape hatch must stay silent whether or not
    // any row matched. (A zero-row void-function statement can't be detected
    // here: its VOID marker lives on a row's value and there is no row.)
    if (!has_more && it->ColumnCount() == 1 &&
        it->GetColumnName(0) == "suppress_query_output") {
      exec_dur += std::chrono::steady_clock::now() - query_start;
      continue;
    }

    auto query_result = ExtractQueryResult(&*it, has_more);
    RETURN_IF_ERROR(query_result.status());

    // We want to include the query iteration time (as it's a part of
    // executing SQL and can be non-trivial), and we want to exclude the time
    // spent printing the result (which can be significant for large results),
    // so we materialise the results first, then take the measurement, then
    // print them.
    exec_dur += std::chrono::steady_clock::now() - query_start;

    if (printed_any_result) {
      fprintf(output, "\n");
    }
    printed_any_result = true;
    PrintQueryResultAsCsv(query_result.value(), output);
  }
  if (!executed_any_statement) {
    return base::ErrStatus("No valid SQL to run");
  }

  PERFETTO_ILOG(
      "Query execution time: %" PRIi64 " ms",
      static_cast<int64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(exec_dur)
              .count()));
  return base::OkStatus();
}

base::Status PrintPerfFile(const std::string& perf_file_path,
                           base::TimeNanos t_load,
                           base::TimeNanos t_run) {
  char buf[128];
  size_t count = base::SprintfTrunc(buf, sizeof(buf), "%" PRId64 ",%" PRId64,
                                    static_cast<int64_t>(t_load.count()),
                                    static_cast<int64_t>(t_run.count()));
  if (count == 0) {
    return base::ErrStatus("Failed to write perf data");
  }

  auto fd(base::OpenFile(perf_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666));
  if (!fd) {
    return base::ErrStatus("Failed to open perf file");
  }
  base::WriteAll(fd.get(), buf, count);
  return base::OkStatus();
}

base::Status RunQueries(TraceProcessor* trace_processor,
                        const std::string& queries,
                        bool expect_output) {
  if (expect_output) {
    return RunQueriesAndPrintResult(trace_processor, queries, stdout);
  }
  return RunQueriesWithoutOutput(trace_processor, queries);
}

base::Status RunQueriesFromFile(TraceProcessor* trace_processor,
                                const std::string& query_file_path,
                                bool expect_output) {
  std::string queries;
  if (!base::ReadFile(query_file_path, &queries)) {
    return base::ErrStatus(
        "Unable to read file %s. If you're passing an SQL query, did you mean "
        "to use the -Q flag instead?",
        query_file_path.c_str());
  }
  return RunQueries(trace_processor, queries, expect_output);
}

}  // namespace perfetto::trace_processor
