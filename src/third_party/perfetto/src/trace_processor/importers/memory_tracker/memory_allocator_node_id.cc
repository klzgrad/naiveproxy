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

#include "perfetto/ext/trace_processor/importers/memory_tracker/memory_allocator_node_id.h"

#include <stdio.h>

#include <cinttypes>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto {
namespace trace_processor {

MemoryAllocatorNodeId::MemoryAllocatorNodeId(uint64_t id) : id_(id) {}

MemoryAllocatorNodeId::MemoryAllocatorNodeId() : MemoryAllocatorNodeId(0u) {}

std::string MemoryAllocatorNodeId::ToString() const {
  size_t max_size = 19;  // Max uint64 is 0xFFFFFFFFFFFFFFFF + 1 for null byte.
  std::string buf;
  buf.resize(max_size);
  size_t final_size = base::SprintfTrunc(&buf[0], max_size, "%" PRIu64, id_);
  buf.resize(final_size);  // Cuts off the final null byte.
  return buf;
}

}  // namespace trace_processor
}  // namespace perfetto
