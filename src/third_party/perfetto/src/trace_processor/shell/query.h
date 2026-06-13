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

#ifndef SRC_TRACE_PROCESSOR_SHELL_QUERY_H_
#define SRC_TRACE_PROCESSOR_SHELL_QUERY_H_

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/trace_processor.h"

namespace perfetto::trace_processor {

struct QueryResult {
  std::vector<std::string> column_names;
  std::vector<std::vector<std::string>> rows;
};

base::StatusOr<QueryResult> ExtractQueryResult(Iterator* it, bool has_more);

void PrintQueryResultAsCsv(const QueryResult& result, FILE* output);

base::Status RunQueriesWithoutOutput(TraceProcessor* trace_processor,
                                     const std::string& sql_query);

base::Status RunQueriesAndPrintResult(TraceProcessor* trace_processor,
                                      const std::string& sql_query,
                                      FILE* output);

base::Status RunQueries(TraceProcessor* trace_processor,
                        const std::string& queries,
                        bool expect_output);

base::Status RunQueriesFromFile(TraceProcessor* trace_processor,
                                const std::string& query_file_path,
                                bool expect_output);

base::Status PrintPerfFile(const std::string& perf_file_path,
                           base::TimeNanos t_load,
                           base::TimeNanos t_run);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_SHELL_QUERY_H_
