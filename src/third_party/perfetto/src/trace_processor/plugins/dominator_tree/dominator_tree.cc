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

#include "src/trace_processor/plugins/dominator_tree/dominator_tree.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/plugins/dominator_tree/tables_py.h"
#include "src/trace_processor/sqlite/bindings/sqlite_aggregate_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

namespace {

class Forest;

// Represents a node in the graph which the dominator tree is being computed on.
struct Node {
  uint32_t id;
  bool operator==(const Node& v) const { return id == v.id; }
};

// Represents the "number" (i.e. index) of a node in the spanning tree computed
// by a DFS on the graph.
struct TreeNumber {
  uint32_t i;
  bool operator<(const TreeNumber& o) const { return i < o.i; }
};

// Helper class containing the "global state" used by the Lengauer-Tarjan
// algorithm.
class Graph {
 public:
  // Lengauer-Tarjan Dominators: Step 1.
  void RunDfs(Node root_node);

  // Lengauer-Tarjan Dominators: Step 2 and 3.
  void ComputeSemiDominatorAndPartialDominator(Forest&);

  // Lengauer-Tarjan Dominators: Step 4.
  void ComputeDominators();

  void AddEdge(Node source, Node dest) {
    state_by_node_.resize(std::max<size_t>(
        state_by_node_.size(),
        static_cast<size_t>(std::max(source.id, dest.id)) + 1));
    GetStateForNode(source).successors.push_back(dest);
    GetStateForNode(dest).predecessors.push_back(source);
  }

  // Converts the dominator tree to a table.
  void ToTable(tables::DominatorTreeTable* table, Node root_node) {
    for (uint32_t i = 0; i < node_count_in_tree(); ++i) {
      Node v = GetNodeForTreeNumber(TreeNumber{i});
      NodeState& v_state = GetStateForNode(v);
      tables::DominatorTreeTable::Row r;
      r.node_id = v.id;
      r.dominator_node_id = v == root_node
                                ? std::nullopt
                                : std::make_optional(v_state.dominator.id);
      table->Insert(r);
    }
  }

  // Returns the semi-dominator TreeNumber for a given Node. v must satisfy
  // IsInTree(v).
  TreeNumber GetSemiDominator(Node v) const {
    return *GetStateForNode(v).semi_dominator;
  }

  // Returns whether the given node was visited by RunDfs and is therefore
  // part of the DFS spanning tree.
  bool IsInTree(Node v) const {
    return GetStateForNode(v).semi_dominator.has_value();
  }

  // Returns the number of nodes in the tree (== the number of nodes in
  // the graph.)
  uint32_t node_count_in_tree() const {
    return static_cast<uint32_t>(node_by_tree_number_.size());
  }

  // Returns the "range" of the ids of the range (i.e. max(node id) + 1).
  //
  // This is useful for creating vectors which are indexed by node id.
  uint32_t node_id_range() const {
    return static_cast<uint32_t>(state_by_node_.size());
  }

 private:
  // Struct containing the state needed for each node.
  struct NodeState {
    std::vector<Node> successors;
    std::vector<Node> predecessors;
    std::optional<TreeNumber> tree_parent;
    std::vector<Node> self_as_semi_dominator;
    std::optional<TreeNumber> semi_dominator;
    Node dominator{0};
  };

  const NodeState& GetStateForNode(Node v) const {
    return state_by_node_[v.id];
  }
  NodeState& GetStateForNode(Node v) { return state_by_node_[v.id]; }
  Node& GetNodeForTreeNumber(TreeNumber d) { return node_by_tree_number_[d.i]; }

  std::vector<NodeState> state_by_node_;
  std::vector<Node> node_by_tree_number_;
};

// Implementation of the "union-find" like helper data structure used by the
// Lengauer-Tarjan algorithm.
//
// This corresponds to the "Link" and "Eval" functions in the paper.
class Forest {
 public:
  explicit Forest(uint32_t vertices_count) : state_by_node_(vertices_count) {
    for (uint32_t i = 0; i < vertices_count; ++i) {
      state_by_node_[i].min_semi_dominator_until_ancestor = Node{i};
    }
  }

  // Corresponds to the "Link" function in the paper.
  void Link(Node ancestor, Node descendant) {
    std::optional<Node>& a = state_by_node_[descendant.id].ancestor;
    PERFETTO_DCHECK(!a);
    a = ancestor;
  }

  // Corresponds to the "Eval" function in the paper. Returns nullopt if
  // `vertex` was not visited by the graph's DFS; such a node has no
  // meaningful semi-dominator to report.
  std::optional<Node> GetMinSemiDominatorToAncestor(Node vertex,
                                                    const Graph& graph) {
    if (!graph.IsInTree(vertex)) {
      return std::nullopt;
    }
    NodeState& state = GetStateForNode(vertex);
    if (!state.ancestor) {
      return vertex;
    }
    Compress(vertex, graph);
    return state.min_semi_dominator_until_ancestor;
  }

 private:
  struct NodeState {
    std::optional<Node> ancestor;
    Node min_semi_dominator_until_ancestor;
  };

  // Implements the O(log(n)) path-compression algorithm in the paper: note that
  // we use stack-based recursion to avoid stack-overflows with very large heap
  // graphs.
  void Compress(Node vertex, const Graph& graph) {
    struct CompressState {
      Node current;
      bool recurse_done;
    };
    std::vector<CompressState> states{CompressState{vertex, false}};
    while (!states.empty()) {
      CompressState& s = states.back();
      NodeState& state = GetStateForNode(s.current);
      PERFETTO_CHECK(state.ancestor);
      NodeState& ancestor_state = GetStateForNode(*state.ancestor);
      if (s.recurse_done) {
        states.pop_back();
        Node ancestor_min = ancestor_state.min_semi_dominator_until_ancestor;
        Node self_min = state.min_semi_dominator_until_ancestor;
        if (graph.GetSemiDominator(ancestor_min) <
            graph.GetSemiDominator(self_min)) {
          state.min_semi_dominator_until_ancestor = ancestor_min;
        }
        state.ancestor = ancestor_state.ancestor;
      } else {
        s.recurse_done = true;
        if (auto grand_ancestor = ancestor_state.ancestor; grand_ancestor) {
          states.push_back(CompressState{*state.ancestor, false});
        } else {
          states.pop_back();
        }
      }
    }
  }

  NodeState& GetStateForNode(Node v) { return state_by_node_[v.id]; }

  std::vector<NodeState> state_by_node_;
};

// Lengauer-Tarjan Dominators: Step 1.
void Graph::RunDfs(Node root) {
  struct StackState {
    Node node;
    std::optional<TreeNumber> parent;
  };

  std::vector<StackState> stack{{root, std::nullopt}};
  while (!stack.empty()) {
    StackState stack_state = stack.back();
    stack.pop_back();

    NodeState& s = GetStateForNode(stack_state.node);
    if (s.semi_dominator) {
      continue;
    }

    TreeNumber tree_number{static_cast<uint32_t>(node_by_tree_number_.size())};
    s.tree_parent = stack_state.parent;
    s.semi_dominator = tree_number;
    node_by_tree_number_.push_back(stack_state.node);

    for (auto it = s.successors.rbegin(); it != s.successors.rend(); ++it) {
      stack.emplace_back(StackState{*it, tree_number});
    }
  }
}

// Lengauer-Tarjan Dominators: Step 2 & 3.
void Graph::ComputeSemiDominatorAndPartialDominator(Forest& forest) {
  // Note the >0 is *intentional* as we do *not* want to process the root.
  for (uint32_t i = node_count_in_tree() - 1; i > 0; --i) {
    Node w = GetNodeForTreeNumber(TreeNumber{i});
    NodeState& w_state = GetStateForNode(w);
    for (Node v : w_state.predecessors) {
      auto u = forest.GetMinSemiDominatorToAncestor(v, *this);
      if (!u) {
        continue;
      }
      w_state.semi_dominator =
          std::min(*w_state.semi_dominator, GetSemiDominator(*u));
    }
    NodeState& semi_dominator_state =
        GetStateForNode(GetNodeForTreeNumber(*w_state.semi_dominator));
    semi_dominator_state.self_as_semi_dominator.push_back(w);
    PERFETTO_CHECK(w_state.tree_parent);

    Node w_parent = GetNodeForTreeNumber(*w_state.tree_parent);
    forest.Link(w_parent, w);

    NodeState& w_parent_state = GetStateForNode(w_parent);
    for (Node v : w_parent_state.self_as_semi_dominator) {
      // v is always in-tree: it was pushed onto some node's
      // self_as_semi_dominator list in an earlier iteration of this loop,
      // which only runs over tree nodes.
      Node u = *forest.GetMinSemiDominatorToAncestor(v, *this);
      NodeState& v_state = GetStateForNode(v);
      v_state.dominator =
          GetSemiDominator(u) < v_state.semi_dominator ? u : w_parent;
    }
    w_parent_state.self_as_semi_dominator.clear();
  }
}

// Lengauer-Tarjan Dominators: Step 4.
void Graph::ComputeDominators() {
  // Starting from 1 is intentional as we don't want to process the root node.
  for (uint32_t i = 1; i < node_count_in_tree(); ++i) {
    Node w = GetNodeForTreeNumber(TreeNumber{i});
    NodeState& w_state = GetStateForNode(w);
    Node semi_dominator = GetNodeForTreeNumber(*w_state.semi_dominator);
    if (w_state.dominator == semi_dominator) {
      continue;
    }
    w_state.dominator = GetStateForNode(w_state.dominator).dominator;
  }
}

struct AggCtx : sqlite::AggregateContext<AggCtx> {
  Graph graph;
  std::optional<uint32_t> start_id;
};

}  // namespace

void DominatorTree::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  if (argc != kArgCount) {
    return sqlite::result::Error(
        ctx, "dominator_tree: incorrect number of arguments");
  }
  int64_t raw_source = sqlite3_value_int64(argv[0]);
  int64_t raw_dest = sqlite3_value_int64(argv[1]);
  int64_t raw_start = sqlite3_value_int64(argv[2]);
  if (raw_source < 0 || raw_source > std::numeric_limits<int32_t>::max() ||
      raw_dest < 0 || raw_dest > std::numeric_limits<int32_t>::max() ||
      raw_start < 0 || raw_start > std::numeric_limits<int32_t>::max()) {
    return sqlite::result::Error(
        ctx, "dominator_tree: node ids must be non-negative 32-bit integers");
  }

  auto& agg_ctx = AggCtx::GetOrCreateContextForStep(ctx);
  auto source = static_cast<uint32_t>(raw_source);
  auto dest = static_cast<uint32_t>(raw_dest);
  agg_ctx.graph.AddEdge(Node{source}, Node{dest});
  if (PERFETTO_UNLIKELY(!agg_ctx.start_id)) {
    agg_ctx.start_id = static_cast<uint32_t>(raw_start);
  }
}

void DominatorTree::Final(sqlite3_context* ctx) {
  auto raw_agg_ctx = AggCtx::GetContextOrNullForFinal(ctx);
  auto table = std::make_unique<tables::DominatorTreeTable>(GetUserData(ctx));
  if (auto* agg_ctx = raw_agg_ctx.get(); agg_ctx) {
    Node start_node{static_cast<uint32_t>(*agg_ctx->start_id)};
    Graph& graph = agg_ctx->graph;
    if (start_node.id >= graph.node_id_range()) {
      return sqlite::result::Error(
          ctx, "dominator_tree: root node is not in the graph");
    }
    Forest forest(graph.node_id_range());

    // Execute the Lengauer-Tarjan Dominators algorithm to compute the dominator
    // tree.
    graph.RunDfs(start_node);
    if (graph.node_count_in_tree() <= 1) {
      return sqlite::result::Error(
          ctx,
          "dominator_tree: non empty graph must contain root and another node");
    }
    graph.ComputeSemiDominatorAndPartialDominator(forest);
    graph.ComputeDominators();
    graph.ToTable(table.get(), start_node);
  }
  // Take the computed dominator tree and convert it to a table.
  return sqlite::result::UniquePointer(
      ctx,
      std::make_unique<dataframe::Dataframe>(std::move(table->dataframe())),
      "TABLE");
}

namespace dominator_tree {
namespace {

class DominatorTreePlugin : public Plugin<DominatorTreePlugin> {
 public:
  ~DominatorTreePlugin() override;

  void RegisterAggregateFunctions(
      PerfettoSqlConnection*,
      std::vector<AggregateFunctionRegistration>& out) override {
    StringPool* pool = trace_context_->storage->mutable_string_pool();
    out.push_back(MakeAggregateRegistration<DominatorTree>(pool));
  }
};

DominatorTreePlugin::~DominatorTreePlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<DominatorTreePlugin>();
      },
      DominatorTreePlugin::kPluginId, DominatorTreePlugin::kDepIds.data(),
      DominatorTreePlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace dominator_tree
}  // namespace perfetto::trace_processor
