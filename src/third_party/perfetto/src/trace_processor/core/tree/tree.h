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

#ifndef SRC_TRACE_PROCESSOR_CORE_TREE_TREE_H_
#define SRC_TRACE_PROCESSOR_CORE_TREE_TREE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core {

// Simple, opinionated columnar storage for tree-structured data.
//
// Each column stores dense data as raw bytes (Slab<uint8_t>) with a dense null
// bitvector. Node ids are implicit dense row indices. The parent array contains
// row indices (kNullParent for roots), and every parent precedes its children:
// parent[i] == kNullParent || parent[i] < i. This makes root-to-leaf and
// leaf-to-root operations single forward and reverse passes.
//
// This is intentionally minimal: no sort state, no sparse nulls, and no shared
// ownership. Tree operators consume it and can reuse its allocations.
struct Tree {
  static constexpr uint32_t kNullParent = std::numeric_limits<uint32_t>::max();

  // Index mapping the tree's original node ids (the "id" column produced by
  // BuildTree) to row indices, built once during construction so consumers
  // can resolve ids to rows via FindRow without rebuilding the map.
  //
  // Storage is chosen to match the id shape: identity ids (id == row index)
  // need no storage; a reasonably dense uint32 id range uses a direct index
  // vector; arbitrary ids fall back to a hash map. The default state is
  // identity: a tree without an id column has row indices as its ids by
  // definition, so a default-constructed tree resolves ids correctly.
  struct IdIndex {
    bool identity_ids = true;
    std::vector<uint32_t> dense;  // dense uint32 ids -> row, kNullParent absent
    // Arbitrary ids -> row. Optional because FlatHashMap's constructor is
    // explicit, which would break aggregate initialization of Tree.
    std::optional<base::FlatHashMap<int64_t, uint32_t>> hash;

    // Resolves an id to a row index within a table of |num_rows| rows, or
    // kNullParent if absent (returned as a sentinel rather than wrapped in
    // std::optional as this is on the hot path of consumers). |num_rows|
    // bounds the identity case, where ids are the row indices themselves.
    uint32_t Find(int64_t id, uint32_t num_rows) const {
      if (identity_ids) {
        if (id < 0 || static_cast<uint64_t>(id) >= num_rows) {
          return kNullParent;
        }
        return static_cast<uint32_t>(id);
      }
      if (!dense.empty()) {
        if (id < 0 || static_cast<uint64_t>(id) >= dense.size()) {
          return kNullParent;
        }
        return dense[static_cast<uint32_t>(id)];
      }
      if (hash) {
        const uint32_t* row = hash->Find(id);
        return row ? *row : kNullParent;
      }
      return kNullParent;
    }
  };

  IdIndex id_index;

  // Resolves an original node id to its row index, or kNullParent if absent.
  uint32_t FindRow(int64_t id) const { return id_index.Find(id, row_count); }

  struct Column {
    // Null-typed columns have no payload at all; their null_bv marks every
    // row as null and their data is never read.
    using Type = TypeSet<Int64, Double, String, Null>;

    // Creates a null-typed column of |rows| rows: no payload, every row null.
    static Column CreateNull(uint32_t rows) {
      Column column;
      column.type = Type(Null{});
      column.null_bv = BitVector::CreateWithSize(rows, false);
      return column;
    }

    template <typename T>
    static Column Create(uint32_t rows, bool nullable = false) {
      using TypeTag = typename TypeTagFor<T>::type;
      Column column;
      column.type = Type(TypeTag{});
      column.data = Slab<uint8_t>::Alloc(uint64_t(rows) * sizeof(T));
      if (nullable) {
        column.null_bv = BitVector::CreateWithSize(rows, true);
      }
      return column;
    }

    template <typename T>
    T* unchecked_data() {
      using TypeTag = typename TypeTagFor<T>::type;
      PERFETTO_DCHECK(type.Is<TypeTag>());
      return reinterpret_cast<T*>(data.data());
    }

    template <typename T>
    const T* unchecked_data() const {
      using TypeTag = typename TypeTagFor<T>::type;
      PERFETTO_DCHECK(type.Is<TypeTag>());
      return reinterpret_cast<const T*>(data.data());
    }

    template <typename T>
    Span<T> unchecked_span() {
      return Span<T>(unchecked_data<T>(),
                     unchecked_data<T>() + data.size() / sizeof(T));
    }

    template <typename T>
    Span<const T> unchecked_span() const {
      return Span<const T>(unchecked_data<T>(),
                           unchecked_data<T>() + data.size() / sizeof(T));
    }

    Type type = Type(Int64{});
    Slab<uint8_t> data;
    BitVector null_bv;  // non-empty for nullable columns
  };

  // Given a column name, returns a pointer to the corresponding column, or
  // nullopt if not found.
  std::optional<const Column*> Find(std::string_view find_name) const {
    auto it = std::find(names.begin(), names.end(), find_name);
    if (it == names.end()) {
      return std::nullopt;
    }
    return columns.data() + std::distance(names.begin(), it);
  }

  // Returns the name associated with a column in this tree.
  std::string_view ColumnName(const Column* column) const {
    const auto index =
        static_cast<size_t>(std::distance(columns.data(), column));
    PERFETTO_DCHECK(index < names.size());
    return names[index];
  }

  uint32_t row_count = 0;
  Slab<uint32_t> parent;  // normalized: row indices, kNullParent for roots
  std::vector<std::string> names;
  std::vector<Column> columns;
};

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_TREE_TREE_H_
