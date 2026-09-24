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

#ifndef SRC_TRACE_PROCESSOR_CORE_TREE_TREE_PATH_INTERNER_H_
#define SRC_TRACE_PROCESSOR_CORE_TREE_TREE_PATH_INTERNER_H_

#include <cstdint>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "src/trace_processor/core/util/flex_vector.h"

namespace perfetto::trace_processor::core {

// Builds a parent-before-child tree by interning (parent, key) pairs. Repeated
// pairs return the existing node, which is the primitive needed to merge a
// forest by root-to-node path. Keys are represented by their 64-bit hash, so
// callers explicitly accept hash-collision semantics. The first input row for
// a pair is retained as its representative.
class TreePathInterner {
 public:
  explicit TreePathInterner(uint32_t estimated_nodes);

  uint32_t Intern(uint32_t parent,
                  base::MurmurHashCombiner key_hash,
                  uint32_t representative_row) {
    key_hash.Combine(parent);
    const uint32_t next = static_cast<uint32_t>(parent_.size());
    auto [node, inserted] = nodes_.Insert(key_hash.digest(), next);
    if (inserted) {
      parent_.push_back(parent);
      representative_row_.push_back(representative_row);
    }
    return *node;
  }

  uint32_t size() const { return static_cast<uint32_t>(parent_.size()); }

  uint32_t parent(uint32_t node) const {
    PERFETTO_DCHECK(node < parent_.size());
    return parent_[node];
  }

  uint32_t representative_row(uint32_t node) const {
    PERFETTO_DCHECK(node < representative_row_.size());
    return representative_row_[node];
  }

 private:
  base::FlatHashMapV2<uint64_t, uint32_t, base::AlreadyHashed<uint64_t>> nodes_;
  FlexVector<uint32_t> parent_;
  FlexVector<uint32_t> representative_row_;
};

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_TREE_TREE_PATH_INTERNER_H_
