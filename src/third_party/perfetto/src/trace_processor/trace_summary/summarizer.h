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

#ifndef SRC_TRACE_PROCESSOR_TRACE_SUMMARY_SUMMARIZER_H_
#define SRC_TRACE_PROCESSOR_TRACE_SUMMARY_SUMMARIZER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/trace_processor/summarizer.h"

namespace perfetto::trace_processor {

class DescriptorPool;
class TraceProcessor;

namespace perfetto_sql::generator {
class StructuredQueryGenerator;
}  // namespace perfetto_sql::generator

namespace summary {

// Internal implementation of the public Summarizer interface.
// Manages lazy materialization of structured queries.
//
// Key behaviors:
// - Lazy: Queries materialized only when Query() is called.
// - Change detection: Uses proto hash to detect changes.
// - Dependency propagation: If A changes, dependents B->C->D re-materialize.
// - Table substitution: Unchanged queries reference their materialized tables.
// - Cleanup: All materialized tables are dropped when the SummarizerImpl is
//   destroyed.
class SummarizerImpl : public Summarizer {
 public:
  SummarizerImpl(TraceProcessor* tp,
                 DescriptorPool* descriptor_pool,
                 std::string id);
  ~SummarizerImpl() override;

  SummarizerImpl(const SummarizerImpl&) = delete;
  SummarizerImpl& operator=(const SummarizerImpl&) = delete;

  // Summarizer implementation.
  base::Status UpdateSpec(const uint8_t* spec_data,
                          size_t spec_size,
                          SummarizerUpdateSpecResult* result) override;

  base::Status Query(const std::string& query_id,
                     SummarizerQueryResult* result) override;

 private:
  struct QueryState {
    std::string table_name;
    std::string proto_hash;  // Hash of the structured query proto bytes.
    std::vector<std::string> columns;
    int64_t row_count = 0;
    double duration_ms = 0.0;
    std::optional<std::string> error;

    // For lazy materialization:
    std::vector<uint8_t> proto_data;  // Stored proto for deferred execution.
    // All query IDs this query depends on, extracted recursively from all
    // embedded query fields (inner_query, inner_query_id, join.left_query,
    // filter_to_intervals.base, etc.). Used for transitive invalidation:
    // if any dependency changes, this query must also be re-materialized.
    std::vector<std::string> inner_query_ids;
    bool needs_materialization = true;  // True until successfully materialized.
    std::string old_table_name;  // Old table to drop after new materialization.

    // Analysis results (populated during materialization):
    std::string sql;  // Complete runnable SQL (includes + preambles + query).
    std::string textproto;       // Text proto representation.
    std::string standalone_sql;  // Fully standalone SQL (no materialized refs).
  };

  // Computes a hash of raw proto bytes for change detection.
  std::string ComputeProtoHash(const uint8_t* data, size_t size);

  // Materializes a single query using a pre-configured generator.
  // The generator must have all queries added and modules/preambles executed.
  base::Status MaterializeQuery(
      const std::string& query_id,
      QueryState& state,
      perfetto_sql::generator::StructuredQueryGenerator& generator);

  // Collects all dependencies for a query (in materialization order).
  std::vector<std::string> CollectDependencies(const std::string& query_id);

  // Prepares the generator for materialization (adds queries, executes modules
  // and preambles). Called once per Query() invocation.
  base::Status PrepareGenerator(
      perfetto_sql::generator::StructuredQueryGenerator& generator,
      std::vector<std::vector<uint8_t>>& table_source_protos);

  // Drops all materialized tables.
  void DropAll();

  // Generates standalone SQL for a query (deferred from materialization).
  void GenerateStandaloneSql(QueryState& state);

  TraceProcessor* tp_;
  DescriptorPool* descriptor_pool_;
  std::string id_;
  base::FlatHashMap<std::string, QueryState> query_states_;
  base::FlatHashMap<std::string, bool>
      included_modules_;  // Track included modules.
  uint32_t next_table_id_ = 0;
};

}  // namespace summary
}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_TRACE_SUMMARY_SUMMARIZER_H_
