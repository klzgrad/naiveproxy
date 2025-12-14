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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/structural_tree_partition.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/tables_py.h"
#include "src/trace_processor/sqlite/bindings/sqlite_aggregate_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"

namespace perfetto::trace_processor {

namespace {

constexpr uint32_t kNullParentId = std::numeric_limits<uint32_t>::max();

struct Row {
  uint32_t id;
  uint32_t parent_id;
  uint32_t group;
};

struct AggCtx : sqlite::AggregateContext<AggCtx> {
  std::vector<Row> input;
  std::vector<uint32_t> child_count_by_id;
  std::optional<Row> root;
  uint32_t max_group = 0;
};

struct LookupHelper {
  const Row* ChildrenForIdBegin(uint32_t id) const {
    return rows.data() + child_count_by_id[id];
  }
  const Row* ChildrenForIdEnd(uint32_t id) const {
    return id + 1 == child_count_by_id.size()
               ? rows.data() + rows.size()
               : rows.data() + child_count_by_id[id + 1];
  }
  const std::vector<Row>& rows;
  const std::vector<uint32_t>& child_count_by_id;
};

}  // namespace

void StructuralTreePartition::StructuralTreePartition::Step(
    sqlite3_context* ctx,
    int argc,
    sqlite3_value** argv) {
  if (argc != 3) {
    return sqlite::result::Error(
        ctx, "tree_partition_arg: incorrect number of arguments");
  }

  auto& agg_ctx = AggCtx::GetOrCreateContextForStep(ctx);

  // For performance reasons, we don't typecheck the arguments and assume they
  // are longs.
  auto id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
  auto group = static_cast<uint32_t>(sqlite::value::Int64(argv[2]));

  // Keep track of the maximum group seen.
  agg_ctx.max_group = std::max(agg_ctx.max_group, group);

  // If the parent_id is null, this is the root. Keep track of it there.
  sqlite3_value* parent_id_value = argv[1];
  if (PERFETTO_UNLIKELY(sqlite::value::IsNull(parent_id_value))) {
    if (agg_ctx.root) {
      return sqlite::result::Error(
          ctx,
          "tree_partition: multiple NULL parent_ids. Only one root (i.e. one "
          "NULL parent_id) expected.");
    }
    agg_ctx.root = Row{id, kNullParentId, group};
    if (id >= agg_ctx.child_count_by_id.size()) {
      agg_ctx.child_count_by_id.resize(id + 1);
    }
    return;
  }

  // Otherwise, this is a non-root. Increment the child count of its parent.
  auto parent_id = static_cast<uint32_t>(sqlite::value::Int64(parent_id_value));
  uint32_t max_id = std::max(id, parent_id);
  if (max_id >= agg_ctx.child_count_by_id.size()) {
    agg_ctx.child_count_by_id.resize(max_id + 1);
  }
  agg_ctx.child_count_by_id[parent_id]++;

  // Keep track of all the values.
  agg_ctx.input.push_back(Row{id, parent_id, group});
}

void StructuralTreePartition::Final(sqlite3_context* ctx) {
  auto scoped_agg_ctx = AggCtx::GetContextOrNullForFinal(ctx);

  // The algorithm below computes the strucutal partition of the input tree.
  // We do this in three stages:
  // 1) We counting sort the input rows to be ordered by parent_id: this acts
  //    as a map to lookup the children for a given node.
  // 2) In the first pass (i.e. downard pass, before any children have been
  //    processed) of the DFS, we associate this node with it's
  //    closest ancestor for the same group and then save this ancestor so
  //    we can restore it.
  // 3) In the second pass (i.e the upwards pass, after all children have been
  //    processed), we restore the ancestor for the group for the previous
  //    ancestor. This ensures that any sibling nodes do not accidentally end up
  //    with this node as its ancestor.

  // If Step was never called, this will be null. Don't run the algorithm in
  // that case, causing an empty table to be returned.
  tables::StructuralTreePartitionTable table(GetUserData(ctx));
  if (auto* agg_ctx = scoped_agg_ctx.get(); agg_ctx) {
    // If there is no root, we cannot do anything.
    if (!agg_ctx->root) {
      return sqlite::result::Error(ctx, "tree_partition: no root in tree");
    }

    // Compute the partial sums to give us the positions we should place each
    // row in the output.
    std::partial_sum(agg_ctx->child_count_by_id.cbegin(),
                     agg_ctx->child_count_by_id.cend(),
                     agg_ctx->child_count_by_id.begin());

    // Counting sort the rows to allow looking up the rows fast.
    std::vector<Row> sorted(agg_ctx->input.size());
    for (auto it = agg_ctx->input.rbegin(); it != agg_ctx->input.rend(); ++it) {
      PERFETTO_DCHECK(agg_ctx->child_count_by_id[it->parent_id] > 0);
      uint32_t index = --agg_ctx->child_count_by_id[it->parent_id];
      sorted[index] = *it;
    }

    struct StackState {
      Row row;
      std::optional<uint32_t> prev_ancestor_id_for_group;
      bool first_pass_done;
    };

    LookupHelper helper{sorted, agg_ctx->child_count_by_id};
    std::vector<StackState> stack{{*agg_ctx->root, std::nullopt, false}};
    std::vector<std::optional<uint32_t>> ancestor_id_for_group(
        agg_ctx->max_group + 1, std::nullopt);
    while (!stack.empty()) {
      StackState& ss = stack.back();
      if (ss.first_pass_done) {
        // This node has already been processed before: make sure to restore the
        // state of the ancestor id back to what it was before this node was
        // processed.
        ancestor_id_for_group[ss.row.group] = ss.prev_ancestor_id_for_group;
        stack.pop_back();
        continue;
      }
      table.Insert(
          {ss.row.id, ancestor_id_for_group[ss.row.group], ss.row.group});

      // Keep track of the fact this node was processed and update the ancestor
      // id for all children.
      ss.first_pass_done = true;
      ss.prev_ancestor_id_for_group = ancestor_id_for_group[ss.row.group];
      ancestor_id_for_group[ss.row.group] = ss.row.id;

      const auto* start = helper.ChildrenForIdBegin(ss.row.id);
      const auto* end = helper.ChildrenForIdEnd(ss.row.id);
      for (const auto* it = start; it != end; ++it) {
        stack.emplace_back(StackState{*it, std::nullopt, false});
      }
    }
  }
  return sqlite::result::UniquePointer(
      ctx, std::make_unique<dataframe::Dataframe>(std::move(table.dataframe())),
      "TABLE");
}

}  // namespace perfetto::trace_processor
