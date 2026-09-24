/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/plugins/graph_traversal/graph_traversal.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/circular_queue.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/perfetto_sql/engine/dataframe_module.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/perfetto_sql/intrinsics/types/array.h"
#include "src/trace_processor/perfetto_sql/intrinsics/types/node.h"
#include "src/trace_processor/plugins/graph_traversal/tables_py.h"
#include "src/trace_processor/sqlite/bindings/sqlite_aggregate_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

namespace {

struct State {
  uint32_t id;
  std::optional<uint32_t> parent_id;
};

// An SQL aggregate-function which performs a DFS from a given start node in a
// graph and returns all the nodes which are reachable from the start node.
//
// Note: this function is not intended to be used directly from SQL: instead
// macros exist in the standard library, wrapping it and making it
// user-friendly.
struct Dfs : public sqlite::AggregateFunction<Dfs> {
  static constexpr char kName[] = "__intrinsic_dfs";
  static constexpr int kArgCount = 2;
  using UserData = StringPool;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == kArgCount);

    auto* graph = sqlite::value::Pointer<perfetto_sql::Graph>(argv[0], "GRAPH");
    auto table = std::make_unique<tables::TreeTable>(GetUserData(ctx));
    if (!graph) {
      return sqlite::result::UniquePointer(
          ctx,
          std::make_unique<dataframe::Dataframe>(std::move(table->dataframe())),
          "TABLE");
    }
    PERFETTO_DCHECK(!graph->empty());

    // If the array is empty, be forgiving and return an empty array. We could
    // return an error here but in 99% of cases, the caller will simply want
    // an empty table instead.
    auto* start_ids =
        sqlite::value::Pointer<perfetto_sql::IntArray>(argv[1], "ARRAY<LONG>");
    if (!start_ids) {
      return sqlite::result::UniquePointer(
          ctx,
          std::make_unique<dataframe::Dataframe>(std::move(table->dataframe())),
          "TABLE");
    }
    PERFETTO_DCHECK(!start_ids->empty());

    uint32_t graph_size = static_cast<uint32_t>(graph->size());
    for (int64_t x : *start_ids) {
      if (x < 0 || x > std::numeric_limits<int32_t>::max()) {
        return sqlite::result::Error(
            ctx, "DFS: node ids must be non-negative 32-bit integers");
      }
    }
    std::vector<bool> visited(graph_size);
    std::unordered_set<uint32_t> visited_outside_graph;
    auto mark_visited = [&](uint32_t id) {
      if (id >= graph_size) {
        return visited_outside_graph.insert(id).second;
      }
      if (visited[id]) {
        return false;
      }
      visited[id] = true;
      return true;
    };
    std::vector<State> stack;
    for (int64_t x : *start_ids) {
      stack.emplace_back(State{static_cast<uint32_t>(x), std::nullopt});
    }
    while (!stack.empty()) {
      State state = stack.back();
      stack.pop_back();

      if (!mark_visited(state.id)) {
        continue;
      }
      table->Insert({state.id, state.parent_id});

      if (state.id >= graph_size) {
        continue;
      }
      const auto& children = (*graph)[state.id].outgoing_edges;
      for (auto it = children.rbegin(); it != children.rend(); ++it) {
        stack.emplace_back(State{*it, state.id});
      }
    }
    return sqlite::result::UniquePointer(
        ctx,
        std::make_unique<dataframe::Dataframe>(std::move(table->dataframe())),
        "TABLE");
  }
};

// An SQL aggregate-function which performs a BFS from a given start node in a
// graph and returns all the nodes which are reachable from the start node.
//
// Note: this function is not intended to be used directly from SQL: instead
// macros exist in the standard library, wrapping it and making it
// user-friendly.
struct Bfs : public sqlite::AggregateFunction<Bfs> {
  static constexpr char kName[] = "__intrinsic_bfs";
  static constexpr int kArgCount = 2;
  using UserData = StringPool;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == kArgCount);

    auto* graph = sqlite::value::Pointer<perfetto_sql::Graph>(argv[0], "GRAPH");
    tables::TreeTable data(GetUserData(ctx));
    if (!graph) {
      return sqlite::result::UniquePointer(
          ctx,
          std::make_unique<dataframe::Dataframe>(std::move(data.dataframe())),
          "TABLE");
    }
    PERFETTO_DCHECK(!graph->empty());

    // If the array is empty, be forgiving and return an empty array. We could
    // return an error here but in 99% of cases, the caller will simply want
    // an empty table instead.
    auto* start_ids =
        sqlite::value::Pointer<perfetto_sql::IntArray>(argv[1], "ARRAY<LONG>");
    if (!start_ids) {
      return sqlite::result::UniquePointer(
          ctx,
          std::make_unique<dataframe::Dataframe>(std::move(data.dataframe())),
          "TABLE");
    }
    PERFETTO_DCHECK(!start_ids->empty());

    uint32_t graph_size = static_cast<uint32_t>(graph->size());
    for (int64_t raw_id : *start_ids) {
      if (raw_id < 0 || raw_id > std::numeric_limits<int32_t>::max()) {
        return sqlite::result::Error(
            ctx, "BFS: node ids must be non-negative 32-bit integers");
      }
    }
    std::vector<bool> visited(graph_size);
    std::unordered_set<uint32_t> visited_outside_graph;
    auto mark_visited = [&](uint32_t id) {
      if (id >= graph_size) {
        return visited_outside_graph.insert(id).second;
      }
      if (visited[id]) {
        return false;
      }
      visited[id] = true;
      return true;
    };
    base::CircularQueue<State> queue;
    for (int64_t raw_id : *start_ids) {
      auto id = static_cast<uint32_t>(raw_id);
      if (!mark_visited(id)) {
        continue;
      }
      queue.emplace_back(State{id, std::nullopt});
    }
    while (!queue.empty()) {
      State state = queue.front();
      queue.pop_front();
      data.Insert({state.id, state.parent_id});

      if (state.id >= graph_size) {
        continue;
      }
      auto& node = (*graph)[state.id];
      for (uint32_t n : node.outgoing_edges) {
        if (!mark_visited(n)) {
          continue;
        }
        queue.emplace_back(State{n, state.id});
      }
    }
    return sqlite::result::UniquePointer(
        ctx,
        std::make_unique<dataframe::Dataframe>(std::move(data.dataframe())),
        "TABLE");
  }
};

}  // namespace

namespace graph_traversal {
namespace {

class GraphTraversalPlugin : public Plugin<GraphTraversalPlugin> {
 public:
  ~GraphTraversalPlugin() override;

  void RegisterFunctions(PerfettoSqlConnection*,
                         std::vector<FunctionRegistration>& out) override {
    StringPool* pool = trace_context_->storage->mutable_string_pool();
    out.push_back(MakeFunctionRegistration<Dfs>(pool));
    out.push_back(MakeFunctionRegistration<Bfs>(pool));
  }
};

GraphTraversalPlugin::~GraphTraversalPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<GraphTraversalPlugin>();
      },
      GraphTraversalPlugin::kPluginId, GraphTraversalPlugin::kDepIds.data(),
      GraphTraversalPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace graph_traversal
}  // namespace perfetto::trace_processor
