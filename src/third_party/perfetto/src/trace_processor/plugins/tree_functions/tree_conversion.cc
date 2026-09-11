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

#include "src/trace_processor/plugins/tree_functions/tree_conversion.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/core/tree/tree.h"
#include "src/trace_processor/core/tree/tree_from_dataframe.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/plugins/tree_functions/tree_functions.h"
#include "src/trace_processor/sqlite/bindings/sqlite_aggregate_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

namespace {

struct AggCtx : sqlite::AggregateContext<AggCtx> {
  std::optional<dataframe::AdhocDataframeBuilder> builder;
};

void PushColumnValue(dataframe::AdhocDataframeBuilder* builder,
                     uint32_t col_idx,
                     const core::Tree::Column& column,
                     uint32_t row) {
  switch (column.type.index()) {
    case core::Tree::Column::Type::GetTypeIndex<core::Int64>():
      builder->PushNonNull(col_idx, column.unchecked_data<int64_t>()[row]);
      return;
    case core::Tree::Column::Type::GetTypeIndex<core::Double>():
      builder->PushNonNull(col_idx, column.unchecked_data<double>()[row]);
      return;
    case core::Tree::Column::Type::GetTypeIndex<core::String>():
      builder->PushNonNull(col_idx,
                           column.unchecked_data<StringPool::Id>()[row]);
      return;
    default:
      PERFETTO_FATAL("Unsupported tree column type");
  }
}

base::StatusOr<dataframe::Dataframe> TreeToDataframe(core::Tree tree,
                                                     StringPool* pool) {
  std::vector<std::string> names = {"_tree_id", "_tree_parent_id"};
  names.insert(names.end(), tree.names.begin(), tree.names.end());
  dataframe::AdhocDataframeBuilder builder(
      std::move(names), pool,
      dataframe::AdhocDataframeBuilder::Options{
          {}, dataframe::NullabilityType::kDenseNull});

  bool ok = true;
  for (uint32_t row = 0; row < tree.row_count && ok; ++row) {
    ok = builder.PushNonNull(0, row);
    if (tree.parent[row] == core::Tree::kNullParent) {
      builder.PushNull(1);
    } else {
      ok = ok && builder.PushNonNull(1, tree.parent[row]);
    }
    for (uint32_t col = 0; col < tree.columns.size(); ++col) {
      const core::Tree::Column& column = tree.columns[col];
      if (column.null_bv.size() > 0 && !column.null_bv.is_set(row)) {
        builder.PushNull(col + 2);
      } else {
        PushColumnValue(&builder, col + 2, column, row);
      }
    }
  }
  if (!ok) {
    return builder.status();
  }
  return std::move(builder).Build();
}

}  // namespace

void TreeFromTable::Step(sqlite3_context* ctx,
                         int rargc,
                         sqlite3_value** argv) {
  auto argc = static_cast<uint32_t>(rargc);
  auto& agg = AggCtx::GetOrCreateContextForStep(ctx);
  if (PERFETTO_UNLIKELY(!agg.builder)) {
    if (PERFETTO_UNLIKELY(argc < 4 || argc % 2 != 0)) {
      return sqlite::result::Error(
          ctx, "tree_from_table: incorrect argument layout");
    }
    uint32_t num_cols = argc / 2;
    std::vector<std::string> col_names;
    col_names.reserve(num_cols);
    for (uint32_t i = 0; i < argc; i += 2) {
      SQLITE_ASSIGN_OR_RETURN(
          ctx, auto col_name,
          sqlite::utils::ExtractArgument(argc, argv, "column name", i,
                                         SqlValue::Type::kString));
      col_names.emplace_back(col_name.AsString());
    }
    agg.builder.emplace(std::move(col_names), GetUserData(ctx),
                        dataframe::AdhocDataframeBuilder::Options{
                            {}, dataframe::NullabilityType::kDenseNull, false});
  }
  bool ok = true;
  for (uint32_t col = 0; col < argc / 2 && ok; ++col) {
    sqlite3_value* value = argv[(2 * col) + 1];
    switch (sqlite::value::Type(value)) {
      case sqlite::Type::kInteger:
        ok = agg.builder->PushNonNull(col, sqlite::value::Int64(value));
        break;
      case sqlite::Type::kFloat:
        ok = agg.builder->PushNonNull(col, sqlite::value::Double(value));
        break;
      case sqlite::Type::kText:
        ok = agg.builder->PushNonNull(
            col, GetUserData(ctx)->InternString(sqlite::value::Text(value)));
        break;
      case sqlite::Type::kNull:
        agg.builder->PushNull(col);
        break;
      case sqlite::Type::kBlob:
        return sqlite::result::Error(ctx,
                                     "tree_from_table: blobs are unsupported");
    }
  }
  if (!ok) {
    return sqlite::utils::SetError(ctx, agg.builder->status());
  }
}

void TreeFromTable::Final(sqlite3_context* ctx) {
  auto raw_agg = AggCtx::GetContextOrNullForFinal(ctx);
  if (PERFETTO_UNLIKELY(!raw_agg)) {
    return sqlite::utils::ReturnNullFromFunction(ctx);
  }
  auto& agg = *raw_agg.get();
  PERFETTO_CHECK(agg.builder);
  SQLITE_ASSIGN_OR_RETURN(ctx, auto cols,
                          core::BuildTree(std::move(*agg.builder)));
  return sqlite::utils::MovePointerResult(ctx, std::move(cols), "TREE");
}

void TreeToTable::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  if (argc != 1) {
    return sqlite::result::Error(ctx,
                                 "tree_to_table: expected exactly 1 argument");
  }
  SQLITE_ASSIGN_OR_RETURN(ctx, core::Tree tree,
                          sqlite::utils::TakeMovePointerValue<core::Tree>(
                              argv[0], "TREE", "tree_to_table"));
  SQLITE_ASSIGN_OR_RETURN(ctx, auto df,
                          TreeToDataframe(std::move(tree), GetUserData(ctx)));
  return sqlite::result::UniquePointer(
      ctx, std::make_unique<dataframe::Dataframe>(std::move(df)), "TABLE");
}

// Computes depth and subtree aggregations (object count, self size, native
// size) for a dominator tree in O(N) linear time using two topological passes.
static base::StatusOr<dataframe::Dataframe> TreeDominatorSummaryImpl(
    core::Tree tree,
    StringPool* pool) {
  std::vector<std::string> names = {"id",
                                    "idom_id",
                                    "dominated_obj_count",
                                    "dominated_size_bytes",
                                    "dominated_native_size_bytes",
                                    "depth"};

  dataframe::AdhocDataframeBuilder builder(
      std::move(names), pool,
      dataframe::AdhocDataframeBuilder::Options{
          {dataframe::AdhocColumnType::kInt64,
           dataframe::AdhocColumnType::kInt64,
           dataframe::AdhocColumnType::kInt64,
           dataframe::AdhocColumnType::kInt64,
           dataframe::AdhocColumnType::kInt64,
           dataframe::AdhocColumnType::kInt64},
          dataframe::NullabilityType::kDenseNull});

  uint32_t N = tree.row_count;
  if (N == 0) {
    return std::move(builder).Build();
  }

  if (tree.columns.size() != 5) {
    return base::ErrStatus(
        "__intrinsic_tree_dominator_summary: expected tree with exactly 5 "
        "columns (id, parent_id, self_size, native_size, self_count), got %zu",
        tree.columns.size());
  }
  constexpr uint32_t kInt64Columns[] = {0, 2, 3, 4};
  for (uint32_t column : kInt64Columns) {
    if (!tree.columns[column].type.Is<core::Int64>()) {
      return base::ErrStatus(
          "__intrinsic_tree_dominator_summary: expected id, self_size, "
          "native_size, and self_count columns to be integers");
    }
  }

  // Pre-initialize working vectors. Nodes start at depth=1.
  std::vector<int64_t> subtree_count(N, 0);
  std::vector<int64_t> subtree_size_bytes(N, 0);
  std::vector<int64_t> subtree_native_size_bytes(N, 0);
  std::vector<int64_t> depth(N, 1);

  const auto* ids = tree.columns[0].unchecked_data<int64_t>();
  const auto* self_sizes = tree.columns[2].unchecked_data<int64_t>();
  const auto* native_sizes = tree.columns[3].unchecked_data<int64_t>();
  const auto* counts = tree.columns[4].unchecked_data<int64_t>();

  // Copy initial self-counts into subtree accumulators, skipping nulls if
  // present.
  if (tree.columns[4].null_bv.size() == 0) {
    std::copy(counts, counts + N, subtree_count.begin());
  } else {
    for (uint32_t i = 0; i < N; ++i) {
      if (tree.columns[4].null_bv.is_set(i)) {
        subtree_count[i] = counts[i];
      }
    }
  }

  // Copy initial self-sizes into subtree accumulators, skipping nulls if
  // present.
  if (tree.columns[2].null_bv.size() == 0) {
    std::copy(self_sizes, self_sizes + N, subtree_size_bytes.begin());
  } else {
    for (uint32_t i = 0; i < N; ++i) {
      if (tree.columns[2].null_bv.is_set(i)) {
        subtree_size_bytes[i] = self_sizes[i];
      }
    }
  }

  // Copy initial native-sizes into subtree accumulators, skipping nulls if
  // present.
  if (tree.columns[3].null_bv.size() == 0) {
    std::copy(native_sizes, native_sizes + N,
              subtree_native_size_bytes.begin());
  } else {
    for (uint32_t i = 0; i < N; ++i) {
      if (tree.columns[3].null_bv.is_set(i)) {
        subtree_native_size_bytes[i] = native_sizes[i];
      }
    }
  }

  // 1. Top-down pass: Parent depth is guaranteed to be computed before child
  //    since nodes in `core::Tree` are topologically ordered.
  for (uint32_t i = 0; i < N; ++i) {
    uint32_t p = tree.parent[i];
    if (p != core::Tree::kNullParent) {
      depth[i] = depth[p] + 1;
    }
  }

  // 2. Bottom-up pass: Traverse in reverse topological order so children
  // accumulate
  //    their total dominated subtree sizes/counts into their immediate parent.
  for (uint32_t i = N; i > 0; --i) {
    uint32_t idx = i - 1;
    uint32_t p = tree.parent[idx];
    if (p != core::Tree::kNullParent) {
      subtree_count[p] += subtree_count[idx];
      subtree_size_bytes[p] += subtree_size_bytes[idx];
      subtree_native_size_bytes[p] += subtree_native_size_bytes[idx];
    }
  }

  // 3. Construct and return output dataframe table for Perfetto SQL.
  bool ok = true;
  for (uint32_t i = 0; i < N && ok; ++i) {
    ok = builder.PushNonNull(0, ids[i]);
    uint32_t p = tree.parent[i];
    if (p == core::Tree::kNullParent) {
      builder.PushNull(1);
    } else {
      ok = ok && builder.PushNonNull(1, ids[p]);
    }
    ok = ok && builder.PushNonNull(2, subtree_count[i]);
    ok = ok && builder.PushNonNull(3, subtree_size_bytes[i]);
    ok = ok && builder.PushNonNull(4, subtree_native_size_bytes[i]);
    ok = ok && builder.PushNonNull(5, depth[i]);
  }
  if (!ok) {
    return builder.status();
  }

  return std::move(builder).Build();
}

void TreeDominatorSummary::Step(sqlite3_context* ctx,
                                int argc,
                                sqlite3_value** argv) {
  if (argc != 1) {
    return sqlite::result::Error(
        ctx, "__intrinsic_tree_dominator_summary: expected 1 argument");
  }
  auto tree_or = sqlite::utils::TakeMovePointerValue<core::Tree>(
      argv[0], "TREE", "__intrinsic_tree_dominator_summary");
  if (!tree_or.ok()) {
    if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull) {
      SQLITE_ASSIGN_OR_RETURN(
          ctx, auto empty_df,
          TreeDominatorSummaryImpl(core::Tree{}, GetUserData(ctx)));
      return sqlite::result::UniquePointer(
          ctx, std::make_unique<dataframe::Dataframe>(std::move(empty_df)),
          "TABLE");
    }
    return sqlite::utils::SetError(ctx, tree_or.status());
  }
  SQLITE_ASSIGN_OR_RETURN(
      ctx, auto df,
      TreeDominatorSummaryImpl(std::move(*tree_or), GetUserData(ctx)));
  return sqlite::result::UniquePointer(
      ctx, std::make_unique<dataframe::Dataframe>(std::move(df)), "TABLE");
}

namespace tree_functions {
namespace {

class TreeFunctionsPlugin : public Plugin<TreeFunctionsPlugin> {
 public:
  ~TreeFunctionsPlugin() override;

  void RegisterFunctions(PerfettoSqlConnection*,
                         std::vector<FunctionRegistration>& out) override {
    StringPool* pool = trace_context_->storage->mutable_string_pool();
    out.push_back(MakeFunctionRegistration<TreeToTable>(pool));
    out.push_back(MakeFunctionRegistration<TreeDominatorSummary>(pool));
  }

  void RegisterAggregateFunctions(
      PerfettoSqlConnection*,
      std::vector<AggregateFunctionRegistration>& out) override {
    StringPool* pool = trace_context_->storage->mutable_string_pool();
    out.push_back(MakeAggregateRegistration<TreeFromTable>(pool));
  }
};

TreeFunctionsPlugin::~TreeFunctionsPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<TreeFunctionsPlugin>();
      },
      TreeFunctionsPlugin::kPluginId, TreeFunctionsPlugin::kDepIds.data(),
      TreeFunctionsPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace tree_functions
}  // namespace perfetto::trace_processor
