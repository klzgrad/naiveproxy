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

#ifndef SRC_TRACE_PROCESSOR_CORE_TREE_TREE_COLUMN_OPS_H_
#define SRC_TRACE_PROCESSOR_CORE_TREE_TREE_COLUMN_OPS_H_

#include <cstdint>

#include "perfetto/ext/base/murmur_hash.h"
#include "src/trace_processor/core/tree/tree.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core::tree_ops {

// Extends each row's hash with the column's nullness and value.
void UpdateRowHashes(const Tree::Column&,
                     Span<base::MurmurHashCombiner> hashes);

// Copies selected rows from a column into a new dense column while preserving
// its type and nullability.
Tree::Column Gather(const Tree::Column&, Span<const uint32_t> rows);

}  // namespace perfetto::trace_processor::core::tree_ops

#endif  // SRC_TRACE_PROCESSOR_CORE_TREE_TREE_COLUMN_OPS_H_
