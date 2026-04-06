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
    fprintf(stderr, "column %u = %s\n", c, it->GetColumnName(c).c_str());
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
  auto query_start = std::chrono::steady_clock::now();

  auto it = trace_processor->ExecuteQuery(sql_query);
  RETURN_IF_ERROR(it.Status());

  bool has_more = it.Next();
  RETURN_IF_ERROR(it.Status());

  uint32_t prev_count = it.StatementCount() - 1;
  uint32_t prev_with_output = has_more ? it.StatementWithOutputCount() - 1
                                       : it.StatementWithOutputCount();
  uint32_t prev_without_output_count = prev_count - prev_with_output;
  if (prev_with_output > 0) {
    return base::ErrStatus(
        "Result rows were returned for multiples queries. Ensure that only the "
        "final statement is a SELECT statement or use `suppress_query_output` "
        "to prevent function invocations causing this "
        "error (see "
        "https://perfetto.dev/docs/contributing/"
        "testing#trace-processor-diff-tests).");
  }
  for (uint32_t i = 0; i < prev_without_output_count; ++i) {
    fprintf(output, "\n");
  }
  if (it.ColumnCount() == 0) {
    PERFETTO_DCHECK(!has_more);
    return base::OkStatus();
  }

  auto query_result = ExtractQueryResult(&it, has_more);
  RETURN_IF_ERROR(query_result.status());

  // We want to include the query iteration time (as it's a part of executing
  // SQL and can be non-trivial), and we want to exclude the time spent printing
  // the result (which can be significant for large results), so we materialise
  // the results first, then take the measurement, then print them.
  auto query_end = std::chrono::steady_clock::now();

  PrintQueryResultAsCsv(query_result.value(), output);

  auto dur = query_end - query_start;
  PERFETTO_ILOG(
      "Query execution time: %" PRIi64 " ms",
      static_cast<int64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(dur).count()));
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
