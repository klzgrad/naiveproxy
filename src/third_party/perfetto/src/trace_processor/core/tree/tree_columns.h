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

#ifndef SRC_TRACE_PROCESSOR_CORE_TREE_TREE_COLUMNS_H_
#define SRC_TRACE_PROCESSOR_CORE_TREE_TREE_COLUMNS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/slab.h"

namespace perfetto::trace_processor::core::tree {

// Simple, opinionated columnar storage for tree-structured data.
//
// Each column stores dense data as raw bytes (Slab<uint8_t>) with a
// dense null bitvector. The tree structure is represented as a normalized
// parent array (row indices, kNullParent for roots).
//
// This is intentionally minimal: no sort state, no sparse nulls, no
// shared ownership. Built by TreeColumnsBuilder, consumed by
// TreeTransformer.
struct TreeColumns {
  struct Column {
    StorageType type = StorageType(Int64{});
    uint32_t elem_size = 0;
    Slab<uint8_t> data;
    BitVector null_bv;  // non-empty only if column has nulls
  };

  uint32_t row_count = 0;
  Slab<uint32_t> parent;  // normalized: row indices, kNullParent for roots
  std::vector<std::string> names;
  std::vector<Column> columns;
};

}  // namespace perfetto::trace_processor::core::tree

#endif  // SRC_TRACE_PROCESSOR_CORE_TREE_TREE_COLUMNS_H_
