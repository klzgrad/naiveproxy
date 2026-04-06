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

#include "src/trace_processor/trace_summary/summarizer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/fnv_hash.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/perfetto_sql/generator/structured_query_generator.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/protozero_to_text.h"

#include "protos/perfetto/perfetto_sql/structured_query.pbzero.h"
#include "protos/perfetto/trace_summary/file.pbzero.h"

namespace perfetto::trace_processor {

// Base class destructor (must be defined for the interface).
Summarizer::~Summarizer() = default;

namespace summary {

namespace {

using perfetto_sql::generator::StructuredQueryGenerator;
using TraceSummarySpec = protos::pbzero::TraceSummarySpec;
using PerfettoSqlStructuredQuery = protos::pbzero::PerfettoSqlStructuredQuery;

// Creates a table-source structured query that references a materialized table.
// This is used for unchanged queries to avoid re-running their full SQL.
std::vector<uint8_t> CreateTableSourceQuery(
    const std::string& query_id,
    const std::string& table_name,
    const std::vector<std::string>& columns) {
  protozero::HeapBuffered<PerfettoSqlStructuredQuery> sq;
  sq->set_id(query_id);
  auto* table = sq->set_table();
  table->set_table_name(table_name);
  for (const auto& col : columns) {
    table->add_column_names(col);
  }
  return sq.SerializeAsArray();
}

// Recursively extracts all inner_query_id fields from a structured query proto.
// This is needed because queries can embed other queries (e.g.,
// join.left_query, filter_to_intervals.base), and those embedded queries may
// reference other queries by ID that need to be tracked as dependencies.
void ExtractInnerQueryIds(const uint8_t* data,
                          size_t size,
                          std::vector<std::string>& out_ids) {
  PerfettoSqlStructuredQuery::Decoder query(data, size);

  // Check top-level inner_query_id.
  if (query.has_inner_query_id()) {
    out_ids.push_back(query.inner_query_id().ToStdString());
  }

  // Check embedded inner_query (recursively).
  if (query.has_inner_query()) {
    auto inner = query.inner_query();
    ExtractInnerQueryIds(inner.data, inner.size, out_ids);
  }

  // Check interval_intersect.base and interval_intersect.interval_intersect[].
  if (query.has_interval_intersect()) {
    auto ii = query.interval_intersect();
    using II = PerfettoSqlStructuredQuery::IntervalIntersect;
    II::Decoder ii_dec(ii.data, ii.size);
    if (ii_dec.has_base()) {
      auto base = ii_dec.base();
      ExtractInnerQueryIds(base.data, base.size, out_ids);
    }
    for (auto it = ii_dec.interval_intersect(); it; ++it) {
      ExtractInnerQueryIds(it->data(), it->size(), out_ids);
    }
  }

  // Check experimental_filter_to_intervals.base and .intervals.
  if (query.has_experimental_filter_to_intervals()) {
    auto fti = query.experimental_filter_to_intervals();
    using FTI = PerfettoSqlStructuredQuery::ExperimentalFilterToIntervals;
    FTI::Decoder fti_dec(fti.data, fti.size);
    if (fti_dec.has_base()) {
      auto base = fti_dec.base();
      ExtractInnerQueryIds(base.data, base.size, out_ids);
    }
    if (fti_dec.has_intervals()) {
      auto intervals = fti_dec.intervals();
      ExtractInnerQueryIds(intervals.data, intervals.size, out_ids);
    }
  }

  // Check experimental_join.left_query and .right_query.
  if (query.has_experimental_join()) {
    auto join = query.experimental_join();
    using Join = PerfettoSqlStructuredQuery::ExperimentalJoin;
    Join::Decoder join_dec(join.data, join.size);
    if (join_dec.has_left_query()) {
      auto left = join_dec.left_query();
      ExtractInnerQueryIds(left.data, left.size, out_ids);
    }
    if (join_dec.has_right_query()) {
      auto right = join_dec.right_query();
      ExtractInnerQueryIds(right.data, right.size, out_ids);
    }
  }

  // Check experimental_union.queries[].
  if (query.has_experimental_union()) {
    auto un = query.experimental_union();
    using Union = PerfettoSqlStructuredQuery::ExperimentalUnion;
    Union::Decoder un_dec(un.data, un.size);
    for (auto it = un_dec.queries(); it; ++it) {
      ExtractInnerQueryIds(it->data(), it->size(), out_ids);
    }
  }

  // Check experimental_add_columns.core_query and .input_query.
  if (query.has_experimental_add_columns()) {
    auto ac = query.experimental_add_columns();
    using AC = PerfettoSqlStructuredQuery::ExperimentalAddColumns;
    AC::Decoder ac_dec(ac.data, ac.size);
    if (ac_dec.has_core_query()) {
      auto core = ac_dec.core_query();
      ExtractInnerQueryIds(core.data, core.size, out_ids);
    }
    if (ac_dec.has_input_query()) {
      auto input = ac_dec.input_query();
      ExtractInnerQueryIds(input.data, input.size, out_ids);
    }
  }

  // Check experimental_create_slices.starts_query and .ends_query.
  if (query.has_experimental_create_slices()) {
    auto cs = query.experimental_create_slices();
    using CS = PerfettoSqlStructuredQuery::ExperimentalCreateSlices;
    CS::Decoder cs_dec(cs.data, cs.size);
    if (cs_dec.has_starts_query()) {
      auto starts = cs_dec.starts_query();
      ExtractInnerQueryIds(starts.data, starts.size, out_ids);
    }
    if (cs_dec.has_ends_query()) {
      auto ends = cs_dec.ends_query();
      ExtractInnerQueryIds(ends.data, ends.size, out_ids);
    }
  }

  // Check experimental_counter_intervals.input_query.
  if (query.has_experimental_counter_intervals()) {
    auto ci = query.experimental_counter_intervals();
    using CI = PerfettoSqlStructuredQuery::ExperimentalCounterIntervals;
    CI::Decoder ci_dec(ci.data, ci.size);
    if (ci_dec.has_input_query()) {
      auto input = ci_dec.input_query();
      ExtractInnerQueryIds(input.data, input.size, out_ids);
    }
  }

  // Check experimental_filter_in.base and .match_values.
  if (query.has_experimental_filter_in()) {
    auto fi = query.experimental_filter_in();
    using FI = PerfettoSqlStructuredQuery::ExperimentalFilterIn;
    FI::Decoder fi_dec(fi.data, fi.size);
    if (fi_dec.has_base()) {
      auto base = fi_dec.base();
      ExtractInnerQueryIds(base.data, base.size, out_ids);
    }
    if (fi_dec.has_match_values()) {
      auto match_values = fi_dec.match_values();
      ExtractInnerQueryIds(match_values.data, match_values.size, out_ids);
    }
  }

  // Check sql.dependencies[].query (recursively).
  if (query.has_sql()) {
    auto sql = query.sql();
    using Sql = PerfettoSqlStructuredQuery::Sql;
    Sql::Decoder sql_dec(sql.data, sql.size);
    for (auto it = sql_dec.dependencies(); it; ++it) {
      Sql::Dependency::Decoder dep_dec(it->data(), it->size());
      if (dep_dec.has_query()) {
        auto dep_query = dep_dec.query();
        ExtractInnerQueryIds(dep_query.data, dep_query.size, out_ids);
      }
    }
  }
}

}  // namespace

SummarizerImpl::SummarizerImpl(TraceProcessor* tp,
                               DescriptorPool* descriptor_pool,
                               std::string id)
    : tp_(tp), descriptor_pool_(descriptor_pool), id_(std::move(id)) {}

SummarizerImpl::~SummarizerImpl() {
  DropAll();
}

std::string SummarizerImpl::ComputeProtoHash(const uint8_t* data, size_t size) {
  base::FnvHasher hasher;
  hasher.Update(reinterpret_cast<const char*>(data), size);
  return std::to_string(hasher.digest());
}

base::Status SummarizerImpl::UpdateSpec(const uint8_t* spec_data,
                                        size_t spec_size,
                                        SummarizerUpdateSpecResult* result) {
  TraceSummarySpec::Decoder spec_decoder(spec_data, spec_size);

  // Track which query IDs are in the new spec.
  base::FlatHashMap<std::string, bool> new_query_ids;

  // Parse all queries from the spec and store them.
  for (auto it = spec_decoder.query(); it; ++it) {
    protozero::ProtoDecoder decoder(it->data(), it->size());
    auto id_field =
        decoder.FindField(PerfettoSqlStructuredQuery::kIdFieldNumber);
    if (!id_field) {
      return base::ErrStatus(
          "Query missing required 'id' field: all queries must have an id");
    }
    std::string query_id = id_field.as_std_string();
    std::string proto_hash = ComputeProtoHash(it->data(), it->size());

    // Extract all inner_query_id dependencies (including nested ones).
    std::vector<std::string> inner_query_ids;
    ExtractInnerQueryIds(it->data(), it->size(), inner_query_ids);

    new_query_ids.Insert(query_id, true);

    // Check if this query already exists and is unchanged.
    QueryState* existing = query_states_.Find(query_id);
    if (existing && existing->proto_hash == proto_hash &&
        !existing->needs_materialization && !existing->error) {
      // Query is unchanged and already materialized - keep it.
      // Re-populate proto_data because it was cleared after materialization
      // (to save memory), but we may need it again if a dependency changes
      // and this query needs to be re-materialized transitively.
      existing->proto_data.assign(it->data(), it->data() + it->size());
      existing->inner_query_ids = std::move(inner_query_ids);
      continue;
    }

    // Query is new or changed - store it for lazy materialization.
    QueryState state;
    state.proto_hash = proto_hash;
    state.proto_data.assign(it->data(), it->data() + it->size());
    state.inner_query_ids = std::move(inner_query_ids);
    state.needs_materialization = true;

    // If existing, defer dropping the old table until new materialization.
    // This prevents race conditions where in-flight queries against the old
    // table fail with "no such table" errors.
    if (existing && !existing->table_name.empty()) {
      state.old_table_name = existing->table_name;
    }

    query_states_[query_id] = std::move(state);
  }

  // Drop tables for queries that are no longer in the spec (auto-drop).
  std::vector<std::string> to_remove;
  for (auto state_it = query_states_.GetIterator(); state_it; ++state_it) {
    if (!new_query_ids.Find(state_it.key())) {
      // Query was removed - drop its table.
      if (!state_it.value().table_name.empty()) {
        auto drop_it = tp_->ExecuteQuery("DROP TABLE IF EXISTS " +
                                         state_it.value().table_name);
        while (drop_it.Next()) {
        }
      }
      // Record the drop in the result.
      SummarizerUpdateSpecResult::QuerySyncInfo sync_info;
      sync_info.query_id = state_it.key();
      sync_info.was_dropped = true;
      result->queries.push_back(std::move(sync_info));

      to_remove.push_back(state_it.key());
    }
  }
  for (const auto& key : to_remove) {
    query_states_.Erase(key);
  }

  // Mark dependencies as needing re-materialization if their dependency
  // changed. Repeat until no more changes (handles transitive dependencies).
  bool changes_made = true;
  while (changes_made) {
    changes_made = false;
    for (auto state_it = query_states_.GetIterator(); state_it; ++state_it) {
      QueryState& state = state_it.value();
      if (state.needs_materialization) {
        continue;  // Already marked.
      }
      // Check all dependencies (including nested ones from embedded queries).
      for (const auto& dep_id : state.inner_query_ids) {
        QueryState* dep = query_states_.Find(dep_id);
        if (dep && dep->needs_materialization) {
          // Dependency needs re-materialization, so does this query.
          state.needs_materialization = true;
          // Defer dropping the old table until new materialization.
          if (!state.table_name.empty()) {
            state.old_table_name = state.table_name;
            state.table_name.clear();
          }
          changes_made = true;
          break;  // Already marked, no need to check other deps.
        }
      }
    }
  }

  // Report status for all queries (no materialization yet - that's lazy).
  for (auto state_it = query_states_.GetIterator(); state_it; ++state_it) {
    SummarizerUpdateSpecResult::QuerySyncInfo sync_info;
    sync_info.query_id = state_it.key();
    sync_info.was_updated = state_it.value().needs_materialization;
    result->queries.push_back(std::move(sync_info));
  }

  return base::OkStatus();
}

std::vector<std::string> SummarizerImpl::CollectDependencies(
    const std::string& query_id) {
  std::vector<std::string> deps;
  base::FlatHashMap<std::string, bool> visited;

  // Walk up the dependency chain and collect in reverse order.
  std::vector<std::string> stack;
  stack.push_back(query_id);

  while (!stack.empty()) {
    std::string current = stack.back();
    stack.pop_back();

    if (visited.Find(current)) {
      continue;
    }
    visited.Insert(current, true);

    QueryState* state = query_states_.Find(current);
    if (!state) {
      continue;
    }

    // Add all dependencies (so they get materialized before this query).
    for (const auto& dep_id : state->inner_query_ids) {
      stack.push_back(dep_id);
    }

    deps.push_back(current);
  }

  // Reverse to get dependencies before dependents.
  std::reverse(deps.begin(), deps.end());
  return deps;
}

base::Status SummarizerImpl::PrepareGenerator(
    StructuredQueryGenerator& generator,
    std::vector<std::vector<uint8_t>>& table_source_protos) {
  // Add ALL queries from query_states_ to the generator.
  // For already-materialized queries, use table-source to avoid re-running SQL.
  for (auto it = query_states_.GetIterator(); it; ++it) {
    const std::string& dep_id = it.key();
    const QueryState& dep_state = it.value();

    if (!dep_state.needs_materialization && !dep_state.table_name.empty()) {
      // Already materialized - use table-source query.
      table_source_protos.push_back(CreateTableSourceQuery(
          dep_id, dep_state.table_name, dep_state.columns));
      const auto& proto = table_source_protos.back();
      auto add_result = generator.AddQuery(proto.data(), proto.size());
      if (!add_result.ok()) {
        return base::ErrStatus("Failed to add table-source query for '%s': %s",
                               dep_id.c_str(), add_result.status().c_message());
      }
    } else {
      // Not materialized - use full proto.
      auto add_result = generator.AddQuery(dep_state.proto_data.data(),
                                           dep_state.proto_data.size());
      if (!add_result.ok()) {
        return base::ErrStatus("Failed to add query '%s': %s", dep_id.c_str(),
                               add_result.status().c_message());
      }
    }
  }

  // Compute and execute modules/preambles ONCE.
  auto modules = generator.ComputeReferencedModules();
  auto preambles = generator.ComputePreambles();

  // Include referenced modules (skip if already included).
  for (const auto& module : modules) {
    if (included_modules_.Find(module)) {
      continue;
    }
    auto mod_it = tp_->ExecuteQuery("INCLUDE PERFETTO MODULE " + module);
    while (mod_it.Next()) {
    }
    if (!mod_it.Status().ok()) {
      return base::ErrStatus("Failed to include module '%s': %s",
                             module.c_str(), mod_it.Status().c_message());
    }
    included_modules_.Insert(module, true);
  }

  // Execute preambles.
  for (const auto& preamble : preambles) {
    auto preamble_it = tp_->ExecuteQuery(preamble);
    while (preamble_it.Next()) {
    }
    if (!preamble_it.Status().ok()) {
      return base::ErrStatus("Failed to execute preamble: %s",
                             preamble_it.Status().c_message());
    }
  }

  return base::OkStatus();
}

base::Status SummarizerImpl::MaterializeQuery(
    const std::string& query_id,
    QueryState& state,
    StructuredQueryGenerator& generator) {
  // Generate SQL for this query.
  // Use inline_shared_queries=true so that shared queries (referenced via
  // inner_query_id) are included as CTEs in the generated SQL rather than being
  // expected to exist as external tables.
  auto sql_result =
      generator.Generate(state.proto_data.data(), state.proto_data.size(),
                         /*inline_shared_queries=*/true);
  if (!sql_result.ok()) {
    state.error = "Failed to generate SQL for query '" + query_id +
                  "': " + sql_result.status().message();
    state.needs_materialization = false;  // Don't retry.
    return sql_result.status();
  }

  const std::string& query_sql = *sql_result;

  // Build complete runnable SQL with includes and preambles (for display).
  // Get these from the generator (already computed during PrepareGenerator).
  auto modules = generator.ComputeReferencedModules();
  auto preambles = generator.ComputePreambles();
  std::string complete_sql;
  for (const auto& module : modules) {
    complete_sql += "INCLUDE PERFETTO MODULE " + module + ";\n";
  }
  for (const auto& preamble : preambles) {
    complete_sql += preamble + "\n";
  }
  if (!complete_sql.empty()) {
    complete_sql += "\n";  // Extra newline before main query.
  }
  complete_sql += query_sql;
  state.sql = complete_sql;

  // Generate textproto representation (if descriptor pool is available).
  if (descriptor_pool_) {
    state.textproto = protozero_to_text::ProtozeroToText(
        *descriptor_pool_, ".perfetto.protos.PerfettoSqlStructuredQuery",
        protozero::ConstBytes{state.proto_data.data(), state.proto_data.size()},
        protozero_to_text::kIncludeNewLines);
  }

  // Note: Standalone SQL generation is deferred to Query() time via
  // GenerateStandaloneSql(). This avoids O(N²) work during batch
  // materialization since each call would otherwise iterate all queries.

  // Generate a new table name. Include the summarizer id so that multiple
  // summarizer instances can coexist without table name collisions.
  std::string table_name =
      "_exp_mat_" + id_ + "_" + std::to_string(next_table_id_++);

  // Track timing for materialization.
  auto start_time = base::GetWallTimeNs();

  // Materialize the query.
  std::string create_sql =
      "CREATE PERFETTO TABLE " + table_name + " AS " + query_sql;
  auto create_it = tp_->ExecuteQuery(create_sql);
  while (create_it.Next()) {
  }

  auto end_time = base::GetWallTimeNs();
  // GetWallTimeNs() returns nanoseconds; divide by 1e6 to convert to ms.
  state.duration_ms =
      static_cast<double>((end_time - start_time).count()) / 1e6;

  if (!create_it.Status().ok()) {
    state.error = create_it.Status().message();
    state.needs_materialization = false;  // Don't retry.
    return create_it.Status();
  }

  // Get column information and row count from the materialized table.
  auto schema_it =
      tp_->ExecuteQuery("SELECT * FROM " + table_name + " LIMIT 0");
  uint32_t col_count = schema_it.ColumnCount();
  state.columns.clear();
  for (uint32_t i = 0; i < col_count; ++i) {
    state.columns.push_back(schema_it.GetColumnName(i));
  }
  while (schema_it.Next()) {
  }

  // Get row count.
  auto count_it = tp_->ExecuteQuery("SELECT COUNT(*) FROM " + table_name);
  if (count_it.Next()) {
    state.row_count = count_it.Get(0).AsLong();
  }

  state.table_name = table_name;
  state.error = std::nullopt;
  state.needs_materialization = false;
  // Note: We intentionally keep proto_data after materialization.
  // It's needed when generating standalone_sql for dependent queries that
  // are materialized later in the same batch. The memory cost is acceptable
  // because the client re-sends the full spec on each sync anyway.

  // Now that the new table is created, drop the old one if it exists.
  // This deferred drop prevents race conditions where in-flight queries
  // against the old table would fail with "no such table" errors.
  if (!state.old_table_name.empty()) {
    auto drop_it =
        tp_->ExecuteQuery("DROP TABLE IF EXISTS " + state.old_table_name);
    while (drop_it.Next()) {
    }
    // Drop errors are silently ignored - if the table is still locked by
    // in-flight queries, it will be cleaned up later.
    state.old_table_name.clear();
  }

  return base::OkStatus();
}

void SummarizerImpl::GenerateStandaloneSql(QueryState& state) {
  // Skip if already generated.
  if (!state.standalone_sql.empty()) {
    return;
  }

  // Fallback to execution SQL if proto_data is missing.
  if (state.proto_data.empty()) {
    state.standalone_sql = state.sql;
    return;
  }

  // Generate standalone SQL using original proto data for all queries
  // (no table-source substitutions). This produces SQL that can be copied
  // and run anywhere without depending on materialized tables.
  StructuredQueryGenerator standalone_generator;
  for (auto it = query_states_.GetIterator(); it; ++it) {
    const QueryState& dep_state = it.value();
    if (dep_state.proto_data.empty()) {
      continue;  // Skip queries without proto data.
    }
    auto add_result = standalone_generator.AddQuery(
        dep_state.proto_data.data(), dep_state.proto_data.size());
    if (!add_result.ok()) {
      // If we can't add a query, fall back to the execution SQL.
      state.standalone_sql = state.sql;
      return;
    }
  }

  auto standalone_sql_result = standalone_generator.Generate(
      state.proto_data.data(), state.proto_data.size(),
      /*inline_shared_queries=*/true);
  if (!standalone_sql_result.ok()) {
    // Fall back to execution SQL if generation fails.
    state.standalone_sql = state.sql;
    return;
  }

  // Build complete standalone SQL with includes and preambles.
  auto standalone_modules = standalone_generator.ComputeReferencedModules();
  auto standalone_preambles = standalone_generator.ComputePreambles();
  std::string standalone_complete;
  for (const auto& module : standalone_modules) {
    standalone_complete += "INCLUDE PERFETTO MODULE " + module + ";\n";
  }
  for (const auto& preamble : standalone_preambles) {
    standalone_complete += preamble + "\n";
  }
  if (!standalone_complete.empty()) {
    standalone_complete += "\n";
  }
  standalone_complete += *standalone_sql_result;
  state.standalone_sql = standalone_complete;
}

void SummarizerImpl::DropAll() {
  for (auto it = query_states_.GetIterator(); it; ++it) {
    if (!it.value().table_name.empty()) {
      auto drop_it =
          tp_->ExecuteQuery("DROP TABLE IF EXISTS " + it.value().table_name);
      while (drop_it.Next()) {
      }
      // Ignore errors during drop.
    }
    // Also drop old tables that haven't been cleaned up yet (e.g., if
    // UpdateSpec() marked a query for re-materialization but Query() was
    // never called to complete the swap).
    if (!it.value().old_table_name.empty()) {
      auto drop_it = tp_->ExecuteQuery("DROP TABLE IF EXISTS " +
                                       it.value().old_table_name);
      while (drop_it.Next()) {
      }
    }
  }
  query_states_.Clear();
}

base::Status SummarizerImpl::Query(const std::string& query_id,
                                   SummarizerQueryResult* result) {
  QueryState* state = query_states_.Find(query_id);
  if (!state) {
    result->exists = false;
    return base::OkStatus();
  }

  result->exists = true;

  // Lazy materialization: materialize if needed.
  if (state->needs_materialization) {
    // Prepare the generator ONCE (adds all queries, executes
    // modules/preambles).
    StructuredQueryGenerator generator;
    std::vector<std::vector<uint8_t>> table_source_protos;
    auto prepare_status = PrepareGenerator(generator, table_source_protos);
    if (!prepare_status.ok()) {
      state->error = prepare_status.message();
      return prepare_status;
    }

    // Materialize dependencies first.
    auto deps = CollectDependencies(query_id);
    for (const auto& dep_id : deps) {
      if (dep_id == query_id) {
        continue;  // Handle the target query last.
      }
      QueryState* dep_state = query_states_.Find(dep_id);
      if (dep_state && dep_state->needs_materialization) {
        auto status = MaterializeQuery(dep_id, *dep_state, generator);
        if (!status.ok()) {
          // Dependency failed - propagate error.
          state->error =
              "Dependency '" + dep_id + "' failed: " + status.message();
          state->needs_materialization = false;
        }
      }
    }

    // Now materialize the target query (if dependencies succeeded).
    if (!state->error) {
      auto status = MaterializeQuery(query_id, *state, generator);
      if (!status.ok()) {
        return status;
      }
    }
  }

  // If there's a stored error from a previous failed materialization, return
  // it.
  if (state->error) {
    return base::ErrStatus("%s", state->error->c_str());
  }

  // Generate standalone SQL lazily (only when Query() is called for this node).
  // This avoids O(N²) work during batch materialization.
  GenerateStandaloneSql(*state);

  result->table_name = state->table_name;
  result->row_count = state->row_count;
  result->columns = state->columns;
  result->duration_ms = state->duration_ms;
  result->sql = state->sql;
  result->textproto = state->textproto;
  result->standalone_sql = state->standalone_sql;
  return base::OkStatus();
}

}  // namespace summary
}  // namespace perfetto::trace_processor
