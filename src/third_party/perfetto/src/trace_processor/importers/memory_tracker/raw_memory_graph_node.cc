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

#include "perfetto/ext/trace_processor/importers/memory_tracker/raw_memory_graph_node.h"

namespace perfetto {
namespace trace_processor {

RawMemoryGraphNode::MemoryNodeEntry::MemoryNodeEntry(const std::string& n,
                                                     const std::string& u,
                                                     uint64_t v)
    : name(n), units(u), entry_type(kUint64), value_uint64(v) {}

RawMemoryGraphNode::MemoryNodeEntry::MemoryNodeEntry(const std::string& n,
                                                     const std::string& u,
                                                     const std::string& v)
    : name(n), units(u), entry_type(kString), value_string(v) {}

bool RawMemoryGraphNode::MemoryNodeEntry::operator==(
    const MemoryNodeEntry& rhs) const {
  if (!(name == rhs.name && units == rhs.units && entry_type == rhs.entry_type))
    return false;
  switch (entry_type) {
    case EntryType::kUint64:
      return value_uint64 == rhs.value_uint64;
    case EntryType::kString:
      return value_string == rhs.value_string;
  }
  return false;
}

RawMemoryGraphNode::RawMemoryGraphNode(const std::string& absolute_name,
                                       LevelOfDetail level,
                                       MemoryAllocatorNodeId id)
    : absolute_name_(absolute_name),
      level_of_detail_(level),
      id_(id),
      flags_(Flags::kDefault) {}

RawMemoryGraphNode::RawMemoryGraphNode(
    const std::string& absolute_name,
    LevelOfDetail level,
    MemoryAllocatorNodeId id,
    std::vector<RawMemoryGraphNode::MemoryNodeEntry>&& entries)
    : absolute_name_(absolute_name),
      level_of_detail_(level),
      entries_(std::move(entries)),
      id_(id),
      flags_(Flags::kDefault) {}

}  // namespace trace_processor
}  // namespace perfetto
