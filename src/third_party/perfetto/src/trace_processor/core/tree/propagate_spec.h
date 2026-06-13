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

#ifndef SRC_TRACE_PROCESSOR_CORE_TREE_PROPAGATE_SPEC_H_
#define SRC_TRACE_PROCESSOR_CORE_TREE_PROPAGATE_SPEC_H_

#include <string>

#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/core/common/tree_types.h"

namespace perfetto::trace_processor::core::tree {

struct PropagateSpec {
  PropagateAggOp agg_op;
  std::string source_col_name;
  std::string output_col_name;
};

// Parses a propagate spec string of the form 'AGG(source_col) AS output_col'.
// Case-insensitive for the aggregate function name and AS keyword.
// Whitespace-resilient.
base::StatusOr<PropagateSpec> ParsePropagateSpec(const std::string& spec);

}  // namespace perfetto::trace_processor::core::tree

namespace perfetto::trace_processor {
namespace tree = core::tree;
}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_CORE_TREE_PROPAGATE_SPEC_H_
