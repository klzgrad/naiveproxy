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

#include "src/trace_processor/core/tree/tree_from_dataframe.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/core/tree/tree.h"
#include "src/trace_processor/core/util/flex_vector.h"
#include "src/trace_processor/core/util/ops.h"
#include "src/trace_processor/core/util/slab.h"

namespace perfetto::trace_processor::core {

namespace {

template <typename T>
Slab<uint8_t> GatherColumnData(Span<const T> values,
                               Span<const uint32_t> order,
                               const BitVector* source_non_null,
                               BitVector* output_non_null) {
  auto bytes = static_cast<uint64_t>(values.size()) * sizeof(T);
  auto slab = Slab<uint8_t>::Alloc(bytes);
  T* output_data = reinterpret_cast<T*>(slab.data());
  Span<T> output(output_data, output_data + order.size());
  if (source_non_null) {
    PERFETTO_DCHECK(output_non_null);
    core::ops::GatherNullableRows(values, *source_non_null, output,
                                  output_non_null, order);
  } else {
    PERFETTO_DCHECK(!output_non_null);
    core::ops::GatherRows(values, output, order);
  }
  return slab;
}

Tree::Column MoveRawColumn(dataframe::AdhocDataframeBuilder::RawColumn& rc) {
  Tree::Column tc;
  if (rc.storage) {
    if (rc.storage->type().Is<Int64>()) {
      tc.type = Tree::Column::Type(Int64{});
      auto& values = rc.storage->unchecked_get<Int64>();
      tc.data = std::move(values).TakeSlab().TakeAsBytes();
    } else if (rc.storage->type().Is<Double>()) {
      tc.type = Tree::Column::Type(Double{});
      auto& values = rc.storage->unchecked_get<Double>();
      tc.data = std::move(values).TakeSlab().TakeAsBytes();
    } else if (rc.storage->type().Is<String>()) {
      tc.type = Tree::Column::Type(String{});
      auto& values = rc.storage->unchecked_get<String>();
      tc.data = std::move(values).TakeSlab().TakeAsBytes();
    } else {
      PERFETTO_FATAL("Unexpected storage type in raw column");
    }
  } else {
    // Columns which never saw a value have no storage and no value type.
    tc.type = Tree::Column::Type(Null{});
  }
  tc.null_bv = std::move(rc.null_bv);
  return tc;
}

// Converts a raw column into parent-before-child row order. This is the only
// payload copy needed when constructing a Tree from unordered input.
Tree::Column GatherRawColumn(dataframe::AdhocDataframeBuilder::RawColumn& rc,
                             uint32_t row_count,
                             Span<const uint32_t> order) {
  Tree::Column tc;
  const BitVector* source_non_null =
      rc.null_bv.size() > 0 ? &rc.null_bv : nullptr;
  if (!rc.storage) {
    // Columns which never saw a value have no storage and no value type.
    tc.type = Tree::Column::Type(Null{});
    tc.null_bv = BitVector::CreateWithSize(row_count);
    return tc;
  }
  if (source_non_null) {
    tc.null_bv = BitVector::CreateWithSize(row_count);
  }
  BitVector* output_non_null = tc.null_bv.size() > 0 ? &tc.null_bv : nullptr;
  if (rc.storage->type().Is<Int64>()) {
    tc.type = Tree::Column::Type(Int64{});
    const auto& values = rc.storage->unchecked_get<Int64>();
    tc.data = GatherColumnData(values.span(), order, source_non_null,
                               output_non_null);
  } else if (rc.storage->type().Is<Double>()) {
    tc.type = Tree::Column::Type(Double{});
    const auto& values = rc.storage->unchecked_get<Double>();
    tc.data = GatherColumnData(values.span(), order, source_non_null,
                               output_non_null);
  } else if (rc.storage->type().Is<String>()) {
    tc.type = Tree::Column::Type(String{});
    const auto& values = rc.storage->unchecked_get<String>();
    tc.data = GatherColumnData(values.span(), order, source_non_null,
                               output_non_null);
  } else {
    PERFETTO_FATAL("Unexpected storage type in raw column");
  }
  return tc;
}

// Direct indexing is used while the dense vector is at least
// 1 / kDenseIndexMaxSizeFactor full; sparser id ranges fall back to the hash
// map to bound the memory wasted on empty slots.
constexpr uint64_t kDenseIndexMaxSizeFactor = 2;

// Builds an id -> row index matching storage to the id shape: identity ids
// (id == row index) need no storage; a reasonably dense uint32 id range uses
// a direct index vector; arbitrary int64 ids fall back to a hash map. Fails
// on duplicate ids.
base::StatusOr<Tree::IdIndex> BuildIdIndex(Span<const int64_t> ids,
                                           uint32_t row_count) {
  Tree::IdIndex result;
  bool identity_ids = true;
  bool uint32_ids = true;
  uint32_t max_id = 0;
  for (uint32_t i = 0; i < row_count; ++i) {
    const int64_t id = ids[i];
    identity_ids = identity_ids && id == i;
    if (id < 0 ||
        static_cast<uint64_t>(id) > std::numeric_limits<uint32_t>::max()) {
      uint32_ids = false;
    } else {
      max_id = std::max(max_id, static_cast<uint32_t>(id));
    }
  }
  result.identity_ids = identity_ids;
  const bool dense_ids =
      !identity_ids && uint32_ids &&
      uint64_t(max_id) + 1 <= uint64_t(row_count) * kDenseIndexMaxSizeFactor;
  if (dense_ids) {
    result.dense.resize(uint64_t(max_id) + 1, Tree::kNullParent);
    for (uint32_t i = 0; i < row_count; ++i) {
      const uint32_t id = static_cast<uint32_t>(ids[i]);
      if (result.dense[id] != Tree::kNullParent) {
        return base::ErrStatus("tree: duplicate id");
      }
      result.dense[id] = i;
    }
  } else if (!identity_ids) {
    result.hash.emplace();
    for (uint32_t i = 0; i < row_count; ++i) {
      auto [row, inserted] = result.hash->Insert(ids[i], i);
      base::ignore_result(row);
      if (!inserted) {
        return base::ErrStatus("tree: duplicate id");
      }
    }
  }
  return std::move(result);
}

base::StatusOr<Tree> BuildFromRawColumns(
    std::vector<dataframe::AdhocDataframeBuilder::RawColumn> raw_cols);

}  // namespace

base::StatusOr<Tree> BuildTree(dataframe::AdhocDataframeBuilder&& builder) {
  ASSIGN_OR_RETURN(auto raw_cols, std::move(builder).BuildRaw());
  return BuildFromRawColumns(std::move(raw_cols));
}

base::StatusOr<Tree> BuildTree(dataframe::RuntimeDataframeBuilder&& builder) {
  ASSIGN_OR_RETURN(auto raw_cols, std::move(builder).BuildRaw());
  return BuildFromRawColumns(std::move(raw_cols));
}

namespace {

base::StatusOr<Tree> BuildFromRawColumns(
    std::vector<dataframe::AdhocDataframeBuilder::RawColumn> raw_cols) {
  // Columns 0 and 1 are id and parent_id.
  if (raw_cols.size() < 2) {
    return base::ErrStatus("tree: need at least id and parent_id columns");
  }

  // Extract id values from column 0. A column without storage and without a
  // null bitmap means the builder has no rows; preserve the column names so
  // consumers can still expose the schema of an empty tree.
  auto& id_rc = raw_cols[0];
  uint32_t row_count = 0;
  const FlexVector<int64_t>* id_vec_ptr = nullptr;
  if (!id_rc.storage) {
    if (id_rc.null_bv.size() != 0) {
      return base::ErrStatus("tree: id column must be integer");
    }
  } else {
    if (!id_rc.storage->type().Is<Int64>()) {
      return base::ErrStatus("tree: id column must be integer");
    }
    id_vec_ptr = &id_rc.storage->unchecked_get<Int64>();
    if (id_vec_ptr->size() >= Tree::kNullParent) {
      return base::ErrStatus("tree: too many rows");
    }
    row_count = static_cast<uint32_t>(id_vec_ptr->size());
  }
  FlexVector<int64_t> empty_ids;
  const auto& id_vec = id_vec_ptr ? *id_vec_ptr : empty_ids;

  Tree result;
  result.row_count = row_count;

  // Id index over the input rows, used to resolve parent_id references during
  // normalization. If the input already satisfies the ordering invariant it is
  // persisted on the tree as-is; a topological reorder invalidates it, in
  // which case it is rebuilt from the output-order id column below.
  ASSIGN_OR_RETURN(auto id_index, BuildIdIndex(id_vec.span(), row_count));

  // Normalize parent_id to input row indices first. Track whether the input
  // already satisfies the Tree ordering invariant while doing so.
  auto& pid_rc = raw_cols[1];
  Slab<uint32_t> input_parent = Slab<uint32_t>::Alloc(row_count);
  std::fill_n(input_parent.data(), row_count, Tree::kNullParent);
  bool identity_order = true;
  if (pid_rc.storage && !pid_rc.storage->type().Is<Int64>()) {
    return base::ErrStatus("tree: parent_id column must be integer");
  }
  if (pid_rc.storage) {
    const auto& pid_vec = pid_rc.storage->unchecked_get<Int64>();
    for (uint32_t row = 0; row < row_count; ++row) {
      if (pid_rc.null_bv.size() > 0 && !pid_rc.null_bv.is_set(row)) {
        continue;
      }
      const uint32_t parent = id_index.Find(pid_vec[row], row_count);
      if (PERFETTO_UNLIKELY(parent == Tree::kNullParent)) {
        return base::ErrStatus("tree: parent_id not found in id column");
      }
      input_parent[row] = parent;
      if (parent >= row) {
        identity_order = false;
      }
    }
  }

  std::vector<uint32_t> order;
  if (identity_order) {
    // A parent-before-child relation cannot contain a cycle. Keep its parent
    // storage and columns in place without constructing any row maps.
    result.parent = std::move(input_parent);
  } else {
    // Topologically order nodes directly through their parent pointers. Use
    // the unfilled suffix of result.parent as the current path stack. Emitting
    // the path parent-first then overwrites each input_parent slot with its
    // output row, fusing parent normalization with topological ordering.
    constexpr uint8_t kVisiting = 1;
    constexpr uint8_t kVisited = 2;
    std::vector<uint8_t> state(row_count);
    order.reserve(row_count);
    result.parent = Slab<uint32_t>::Alloc(row_count);
    for (uint32_t start = 0; start < row_count; ++start) {
      if (state[start] == kVisited) {
        continue;
      }

      uint32_t path_size = 0;
      uint32_t row = start;
      while (row != Tree::kNullParent && state[row] == 0) {
        state[row] = kVisiting;
        result.parent[row_count - ++path_size] = row;
        row = input_parent[row];
      }
      if (row != Tree::kNullParent && state[row] == kVisiting) {
        return base::ErrStatus("tree: cycle detected in parent relation");
      }

      while (path_size > 0) {
        uint32_t input_row = result.parent[row_count - path_size--];
        uint32_t parent = input_parent[input_row];
        uint32_t output_row = static_cast<uint32_t>(order.size());
        result.parent[output_row] = parent == Tree::kNullParent
                                        ? Tree::kNullParent
                                        : input_parent[parent];
        input_parent[input_row] = output_row;
        state[input_row] = kVisited;
        order.push_back(input_row);
      }
    }
  }

  result.names.reserve(raw_cols.size());
  result.columns.reserve(raw_cols.size());
  if (identity_order) {
    for (auto& rc : raw_cols) {
      result.names.push_back(std::move(rc.name));
      result.columns.push_back(MoveRawColumn(rc));
    }
    result.id_index = std::move(id_index);
  } else {
    // Gather all columns (including id/parent_id) in topological order.
    for (auto& rc : raw_cols) {
      result.names.push_back(std::move(rc.name));
      result.columns.push_back(GatherRawColumn(rc, row_count, MakeSpan(order)));
    }
    // The reorder invalidated the input-space index; rebuild it from the
    // gathered id column, which is in output row order.
    const auto* ids = result.columns[0].unchecked_data<int64_t>();
    ASSIGN_OR_RETURN(
        result.id_index,
        BuildIdIndex(Span<const int64_t>(ids, ids + row_count), row_count));
  }
  return std::move(result);
}

}  // namespace
}  // namespace perfetto::trace_processor::core
