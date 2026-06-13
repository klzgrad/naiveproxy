/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "perfetto/ext/trace_processor/importers/memory_tracker/raw_process_memory_node.h"

#include <stdio.h>
#include <stdlib.h>

#include <cinttypes>
#include <functional>
#include <memory>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {

RawProcessMemoryNode::RawProcessMemoryNode(LevelOfDetail level_of_detail,
                                           AllocatorNodeEdgesMap&& edges_map,
                                           MemoryNodesMap&& nodes_map)
    : level_of_detail_(level_of_detail),
      allocator_nodes_edges_(std::move(edges_map)),
      allocator_nodes_(std::move(nodes_map)) {}

RawProcessMemoryNode::RawProcessMemoryNode(RawProcessMemoryNode&&) = default;
RawProcessMemoryNode::~RawProcessMemoryNode() = default;
RawProcessMemoryNode& RawProcessMemoryNode::operator=(RawProcessMemoryNode&&) =
    default;

RawMemoryGraphNode* RawProcessMemoryNode::GetAllocatorNode(
    const std::string& absolute_name) const {
  auto it = allocator_nodes_.find(absolute_name);
  if (it != allocator_nodes_.end())
    return it->second.get();
  return nullptr;
}

}  // namespace trace_processor
}  // namespace perfetto
