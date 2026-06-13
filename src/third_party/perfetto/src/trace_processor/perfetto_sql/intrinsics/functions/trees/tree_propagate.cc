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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/trees/tree_propagate.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/trace_processor/core/tree/propagate_spec.h"
#include "src/trace_processor/core/tree/tree_transformer.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

void TreePropagateDown::Step(sqlite3_context* ctx,
                             int argc,
                             sqlite3_value** argv) {
  if (argc < 2) {
    return sqlite::result::Error(
        ctx,
        "tree_propagate_down: expected at least 2 arguments "
        "(tree_ptr, 'AGG(col) AS alias', ...)");
  }

  // Extract tree pointer.
  auto* tree_ptr =
      sqlite::value::Pointer<sqlite::utils::MovePointer<tree::TreeTransformer>>(
          argv[0], "TREE_TRANSFORMER");
  if (!tree_ptr) {
    return sqlite::result::Error(
        ctx, "tree_propagate_down: expected TREE_TRANSFORMER");
  }
  if (tree_ptr->taken()) {
    return sqlite::result::Error(
        ctx, "tree_propagate_down: tree has already been consumed");
  }

  // Parse each spec string.
  std::vector<tree::PropagateSpec> specs;
  for (int i = 1; i < argc; ++i) {
    if (sqlite::value::Type(argv[i]) != sqlite::Type::kText) {
      return sqlite::result::Error(
          ctx, "tree_propagate_down: spec arguments must be strings");
    }
    std::string spec_str = sqlite::value::Text(argv[i]);
    SQLITE_ASSIGN_OR_RETURN(ctx, auto spec, tree::ParsePropagateSpec(spec_str));
    specs.push_back(std::move(spec));
  }

  auto transformer = tree_ptr->Take();
  auto status = transformer.PropagateDown(std::move(specs));
  SQLITE_RETURN_IF_ERROR(ctx, status);

  return sqlite::result::UniquePointer(
      ctx,
      std::make_unique<sqlite::utils::MovePointer<tree::TreeTransformer>>(
          std::move(transformer)),
      "TREE_TRANSFORMER");
}

}  // namespace perfetto::trace_processor
