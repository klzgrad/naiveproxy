/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/vulkan_memory_tracker.h"

#include <array>
#include <cstddef>
#include <vector>

#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

VulkanMemoryTracker::VulkanMemoryTracker(TraceProcessorContext* context)
    : context_(context) {
  static constexpr std::array kEventSources = {
      "UNSPECIFIED",       "DRIVER",     "DEVICE",
      "GPU_DEVICE_MEMORY", "GPU_BUFFER", "GPU_IMAGE",
  };
  for (const auto& event_source : kEventSources) {
    source_strs_id_.emplace_back(context_->storage->InternString(event_source));
  }

  static constexpr std::array kEventOperations = {
      "UNSPECIFIED", "CREATE",        "DESTROY",
      "BIND",        "DESTROY_BOUND", "ANNOTATIONS",
  };
  for (const auto& event_operation : kEventOperations) {
    operation_strs_id_.emplace_back(
        context_->storage->InternString(event_operation));
  }

  static constexpr std::array kEventScopes = {
      "UNSPECIFIED", "COMMAND", "OBJECT", "CACHE", "DEVICE", "INSTANCE",
  };
  for (const auto& event_scope : kEventScopes) {
    scope_strs_id_.emplace_back(context_->storage->InternString(event_scope));
  }
}

StringId VulkanMemoryTracker::FindSourceString(
    VulkanMemoryEvent::Source source) {
  return source_strs_id_[static_cast<size_t>(source)];
}

StringId VulkanMemoryTracker::FindOperationString(
    VulkanMemoryEvent::Operation operation) {
  return operation_strs_id_[static_cast<size_t>(operation)];
}

StringId VulkanMemoryTracker::FindAllocationScopeString(
    VulkanMemoryEvent::AllocationScope scope) {
  return scope_strs_id_[static_cast<size_t>(scope)];
}

}  // namespace perfetto::trace_processor
