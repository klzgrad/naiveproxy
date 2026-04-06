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

#ifndef INCLUDE_PERFETTO_TRACE_PROCESSOR_SUMMARIZER_H_
#define INCLUDE_PERFETTO_TRACE_PROCESSOR_SUMMARIZER_H_

// EXPERIMENTAL: This API is under active development and may change without
// notice. Do not depend on this interface in production code.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/export.h"
#include "perfetto/base/status.h"

namespace perfetto::trace_processor {

// EXPERIMENTAL: Result of Summarizer::UpdateSpec().
struct PERFETTO_EXPORT_COMPONENT SummarizerUpdateSpecResult {
  // Per-query sync info.
  struct QuerySyncInfo {
    std::string query_id;
    std::optional<std::string> error;
    bool was_updated = false;
    bool was_dropped = false;
  };
  std::vector<QuerySyncInfo> queries;
};

// EXPERIMENTAL: Result of Summarizer::Query().
struct PERFETTO_EXPORT_COMPONENT SummarizerQueryResult {
  bool exists = false;
  std::string table_name;
  int64_t row_count = 0;
  std::vector<std::string> columns;
  double duration_ms = 0.0;
  std::string sql;             // Complete runnable SQL (includes + preambles).
  std::string textproto;       // Text proto representation.
  std::string standalone_sql;  // Fully standalone SQL (no materialized refs).
};

// EXPERIMENTAL: Manages lazy materialization of structured queries.
//
// Key behaviors:
// - Lazy: Queries materialized only when Query() is called.
// - Change detection: Uses proto hash to detect changes.
// - Dependency propagation: If A changes, dependents B->C->D re-materialize.
// - Table substitution: Unchanged queries reference their materialized tables.
// - Cleanup: All materialized tables are dropped when the Summarizer is
//   destroyed.
//
// Obtain an instance via TraceProcessor::CreateSummarizer().
class PERFETTO_EXPORT_COMPONENT Summarizer {
 public:
  virtual ~Summarizer();

  // Updates the spec. Compares proto hashes to detect changes, auto-drops
  // removed queries, marks changed queries for re-materialization.
  // Materialization is lazy (deferred to Query()).
  //
  // The spec should be a serialized TraceSummarySpec proto.
  virtual base::Status UpdateSpec(const uint8_t* spec_data,
                                  size_t spec_size,
                                  SummarizerUpdateSpecResult* result) = 0;

  // Fetches a query result, materializing on demand if needed.
  // Returns OK with exists=false if query_id not found.
  virtual base::Status Query(const std::string& query_id,
                             SummarizerQueryResult* result) = 0;
};

}  // namespace perfetto::trace_processor

#endif  // INCLUDE_PERFETTO_TRACE_PROCESSOR_SUMMARIZER_H_
