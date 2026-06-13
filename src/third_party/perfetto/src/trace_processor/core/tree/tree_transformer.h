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
#include <string_view>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/tree/propagate_spec.h"
#include "src/trace_processor/core/tree/tree_columns.h"

namespace perfetto::trace_processor::core::interpreter {
class BytecodeBuilder;
}  // namespace perfetto::trace_processor::core::interpreter

namespace perfetto::trace_processor::core::tree {

// Transforms a tree-structured dataset via composable operations (filtering,
// propagation) producing a dataframe.
//
// Takes ownership of TreeColumns in the constructor. Bytecodes are emitted
// incrementally as methods are called. ToDataframe() finalizes the bytecode,
// executes it, and builds the output.
//
// Usage:
//   TreeTransformer t(std::move(cols), pool);
//   RETURN_IF_ERROR(t.FilterTree(specs, values));
//   RETURN_IF_ERROR(t.PropagateDown(propagate_specs));
//   RETURN_IF_ERROR(t.FilterTree(specs2, values2));  // can filter on
//   propagated ASSIGN_OR_RETURN(auto result, std::move(t).ToDataframe());
class TreeTransformer {
 public:
  TreeTransformer(TreeColumns cols, StringPool* pool);
  ~TreeTransformer();

  TreeTransformer(TreeTransformer&&) noexcept;
  TreeTransformer& operator=(TreeTransformer&&) noexcept;

  // Resolves a column name to its index in the owned column storage.
  std::optional<uint32_t> ResolveColumn(std::string_view name) const;

  // Returns the storage type for a column at the given index.
  StorageType column_type(uint32_t col) const {
    return cols_.columns[col].type;
  }

  // Applies a filter to the tree. Nodes matching the filter are kept;
  // filtered-out nodes have their children reparented to the closest
  // surviving ancestor. Can be called multiple times; bytecodes are
  // emitted immediately.
  base::Status FilterTree(std::vector<dataframe::FilterSpec> specs,
                          std::vector<SqlValue> values);

  // Propagates column values from root toward leaves. For each spec, a new
  // output column is created and filled by copying the source column and
  // then applying the aggregate operation downward through the tree via BFS.
  //
  // After this call, the new columns can be referenced by name in subsequent
  // FilterTree() calls.
  base::Status PropagateDown(std::vector<PropagateSpec> specs);

  // Finalizes bytecode, executes it, and returns the resulting dataframe.
  base::StatusOr<dataframe::Dataframe> ToDataframe() &&;

 private:
  struct RegInit {
    enum Kind : uint8_t { kStorage, kNullBv };
    Kind kind;
    uint32_t reg;
    uint32_t col;  // index into cols_.columns
  };

  struct PropagateInfo {
    uint32_t source_col;  // cols_.columns index
    uint32_t dest_col;    // cols_.columns index
    PropagateAggOp agg_op;
  };

  TreeColumns cols_;
  StringPool* pool_;

  // Bytecode builder — accumulates bytecodes as methods are called.
  std::unique_ptr<interpreter::BytecodeBuilder> builder_;

  // Register indices (allocated in constructor, shared across calls).
  uint32_t span_reg_index_ = 0;
  uint32_t tree_state_reg_index_ = 0;

  // Register initialization info collected during FilterTree calls.
  std::vector<RegInit> reg_inits_;

  // Filter values accumulated across FilterTree calls.
  std::vector<SqlValue> filter_values_;
  uint32_t filter_value_count_ = 0;

  // Propagation specs accumulated across PropagateDown calls.
  std::vector<PropagateInfo> propagate_specs_;

  // Whether any bytecodes have been emitted.
  bool has_operations_ = false;
};

}  // namespace perfetto::trace_processor::core::tree

namespace perfetto::trace_processor {
namespace tree = core::tree;
}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_CORE_TREE_TREE_TRANSFORMER_H_
