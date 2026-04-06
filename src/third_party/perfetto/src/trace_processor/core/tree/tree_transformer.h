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

#ifndef SRC_TRACE_PROCESSOR_CORE_TREE_TREE_TRANSFORMER_H_
#define SRC_TRACE_PROCESSOR_CORE_TREE_TREE_TRANSFORMER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/small_vector.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/dataframe_register_cache.h"
#include "src/trace_processor/core/dataframe/query_plan.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/interpreter/bytecode_builder.h"
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core::tree {

// Transforms a tree-structured dataframe via a bunch of operations producing
// another dataframe.
//
// The tree structure is represented by the first two columns of the dataframe:
// - Column 0: node ID (the original ID, or _tree_id if already transformed)
// - Column 1: parent ID (parent node ID, or _tree_parent_id)
//
// Operations can be chained:
//   auto status = TreeTransformer(df, pool)
//       .FilterTree(filter_specs);  // Emits bytecode immediately
//   RETURN_IF_ERROR(status);
//   auto result = std::move(transformer).ToDataframe();
//
// All operations emit bytecode immediately. ToDataframe() executes the
// accumulated bytecode and uses SelectRows to create the final dataframe.
//
// Architecture follows QueryPlanBuilder patterns:
// - Uses builder_.GetOrCreateScratchRegisters() for scratch management
// - Uses scope-based register caching via builder_.CreateCacheScope()
// - Single unified code path (no lazy initialization)
class TreeTransformer {
 public:
  explicit TreeTransformer(dataframe::Dataframe df, StringPool* pool);

  // Applies a filter to the tree, keeping only nodes matching the filter
  // and reparenting surviving children to their closest surviving ancestor.
  //
  // The filter specs are evaluated against the dataframe columns. Nodes
  // that pass the filter are kept; nodes that are filtered out have their
  // children reparented to the closest surviving ancestor.
  //
  // Multiple FilterTree calls can be chained; they are applied in order.
  // Bytecode is emitted immediately for each call.
  //
  // Returns Status (can fail if filter specs reference invalid columns).
  base::Status FilterTree(std::vector<dataframe::FilterSpec> specs,
                          std::vector<SqlValue> values);

  // Returns the underlying dataframe (for accessing column metadata).
  const dataframe::Dataframe& df() const { return df_; }

  // Transforms the tree and returns the resulting dataframe.
  //
  // This always executes via a unified path:
  // - If no FilterTree calls: just builds tree structure columns
  // - If FilterTree was called: executes bytecode and filters rows
  base::StatusOr<dataframe::Dataframe> ToDataframe() &&;

 private:
  // Initializes tree structure on first FilterTree call.
  // Allocates persistent parent/original_rows spans, all scratch buffers,
  // and emits MakeChildToParentTreeStructure bytecode.
  void InitializeTreeStructure(uint32_t row_count);

  // Emits MakeParentToChildTreeStructure bytecode if P2C is stale.
  // Uses pre-allocated scratch buffers stored as member variables.
  // Sets p2c_stale_ to false after emitting.
  void EnsureParentToChildStructure();

  // Generates filter bytecode and converts result to a bitvector.
  base::StatusOr<interpreter::RwHandle<BitVector>> BuildFilterBitvector(
      uint32_t row_count,
      std::vector<dataframe::FilterSpec>& specs);

  // Emits FilterTree bytecode with all required registers.
  // Uses pre-allocated scratch buffers stored as member variables.
  void EmitFilterTreeBytecode(interpreter::RwHandle<BitVector> keep_bv);

  dataframe::Dataframe df_;
  StringPool* pool_;

  // Bytecode builder for accumulating tree operations.
  // Heap-allocated so that cache_ (which holds a reference) remains valid
  // when TreeTransformer is moved.
  std::unique_ptr<interpreter::BytecodeBuilder> builder_;

  // Register cache for caching column/index registers.
  // Heap-allocated so the internal reference to builder_ survives moves.
  std::unique_ptr<dataframe::DataframeRegisterCache> cache_;

  // Parent and original_rows span registers (set on first FilterTree call).
  interpreter::RwHandle<Span<uint32_t>> parent_span_;
  interpreter::RwHandle<Span<uint32_t>> original_rows_span_;

  // Register holding the normalized parent_id storage (set at execution time).
  interpreter::ReadHandle<interpreter::StoragePtr> parent_storage_reg_;

  // Filter values for bytecode execution.
  std::vector<SqlValue> filter_values_;

  // Register initialization specs collected from Filter() calls.
  // Needed to initialize storage registers before bytecode execution.
  base::SmallVector<dataframe::RegisterInit, 16> register_inits_;

  // Alias for scratch register type.
  using Scratch = interpreter::BytecodeBuilder::ScratchRegisters;

  // Scratch buffers allocated once in InitializeTreeStructure() and reused
  // across all FilterTree() calls. This avoids emitting AllocateIndices
  // bytecode for each filter operation. Wrapped in optional to track
  // initialization state.
  struct FilterScratch {
    Scratch scratch1;
    Scratch scratch2;
    Scratch p2c_scratch;
    Scratch p2c_offsets;
    Scratch p2c_children;
    Scratch p2c_roots;
  };
  std::optional<FilterScratch> filter_scratch_;

  // Tracks whether the P2C (parent-to-child) structure is stale and needs
  // rebuilding. Operations that modify C2P (like FilterTree) set this to true.
  // EnsureParentToChildStructure() checks this and rebuilds P2C if needed.
  bool p2c_stale_ = true;
};

}  // namespace perfetto::trace_processor::core::tree

namespace perfetto::trace_processor {

// Namespace alias for ergonomics.
namespace tree = core::tree;

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_CORE_TREE_TREE_TRANSFORMER_H_
