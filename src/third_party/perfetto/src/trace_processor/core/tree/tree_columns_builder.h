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

#ifndef SRC_TRACE_PROCESSOR_CORE_TREE_TREE_COLUMNS_BUILDER_H_
#define SRC_TRACE_PROCESSOR_CORE_TREE_TREE_COLUMNS_BUILDER_H_

#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/runtime_dataframe_builder.h"
#include "src/trace_processor/core/tree/tree_columns.h"

namespace perfetto::trace_processor::core::tree {

// Builds TreeColumns row-by-row.
//
// Wraps RuntimeDataframeBuilder for type inference, null tracking, and
// value pushing. Build() converts the resulting Dataframe into TreeColumns
// with a normalized parent array.
//
// The first two columns must be id and parent_id. They are consumed to
// build the normalized parent array and are also included in the output
// data columns.
//
// Usage:
//   TreeColumnsBuilder b({"id", "parent_id", "name", "dur"}, pool);
//   for (...) {
//     b.AddRow(&fetcher);
//   }
//   ASSIGN_OR_RETURN(auto cols, std::move(b).Build());
class TreeColumnsBuilder {
 public:
  TreeColumnsBuilder(std::vector<std::string> names, StringPool* pool);
  ~TreeColumnsBuilder();

  TreeColumnsBuilder(TreeColumnsBuilder&&) noexcept;
  TreeColumnsBuilder& operator=(TreeColumnsBuilder&&) noexcept;

  // Adds a row using the given fetcher. Column 0 = id, column 1 = parent_id,
  // columns 2+ = data.
  template <typename F>
  bool AddRow(F* fetcher) {
    return builder_.AddRow(fetcher);
  }

  // Finalizes and returns TreeColumns with normalized parent array.
  base::StatusOr<TreeColumns> Build() &&;

  const base::Status& status() const { return builder_.status(); }

 private:
  dataframe::RuntimeDataframeBuilder builder_;
};

}  // namespace perfetto::trace_processor::core::tree

#endif  // SRC_TRACE_PROCESSOR_CORE_TREE_TREE_COLUMNS_BUILDER_H_
