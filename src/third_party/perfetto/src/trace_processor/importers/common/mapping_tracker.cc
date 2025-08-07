/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/common/mapping_tracker.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/jit_cache.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/build_id.h"

namespace perfetto {
namespace trace_processor {
namespace {

bool IsKernelModule(const CreateMappingParams& params) {
  return !base::StartsWith(params.name, "[kernel.kallsyms]") &&
         !params.memory_range.empty();
}

}  // namespace

template <typename MappingImpl>
MappingImpl& MappingTracker::AddMapping(std::unique_ptr<MappingImpl> mapping) {
  auto ptr = mapping.get();
  PERFETTO_CHECK(
      mappings_by_id_.Insert(ptr->mapping_id(), std::move(mapping)).second);

  mappings_by_name_and_build_id_[NameAndBuildId{base::StringView(ptr->name()),
                                                ptr->build_id()}]
      .push_back(ptr);

  return *ptr;
}

KernelMemoryMapping& MappingTracker::CreateKernelMemoryMapping(
    CreateMappingParams params) {
  // TODO(carlscab): Guess build_id if not provided. Some tools like simpleperf
  // add a mapping file_name ->build_id that we could use here

  const bool is_module = IsKernelModule(params);

  if (!is_module && kernel_ != nullptr) {
    PERFETTO_CHECK(params.memory_range == kernel_->memory_range());
    return *kernel_;
  }

  std::unique_ptr<KernelMemoryMapping> mapping(
      new KernelMemoryMapping(context_, std::move(params)));

  if (is_module) {
    kernel_modules_.TrimOverlapsAndEmplace(mapping->memory_range(),
                                           mapping.get());
  } else {
    kernel_ = mapping.get();
  }

  return AddMapping(std::move(mapping));
}

UserMemoryMapping& MappingTracker::CreateUserMemoryMapping(
    UniquePid upid,
    CreateMappingParams params) {
  const AddressRange mapping_range = params.memory_range;
  std::unique_ptr<UserMemoryMapping> mapping(
      new UserMemoryMapping(context_, upid, std::move(params)));

  user_memory_[upid].TrimOverlapsAndEmplace(mapping_range, mapping.get());

  jit_caches_[upid].ForOverlaps(
      mapping_range, [&](std::pair<const AddressRange, JitCache*>& entry) {
        const auto& jit_range = entry.first;
        JitCache* jit_cache = entry.second;
        PERFETTO_CHECK(jit_range.Contains(mapping_range));
        mapping->SetJitCache(jit_cache);
      });

  return AddMapping(std::move(mapping));
}

KernelMemoryMapping* MappingTracker::FindKernelMappingForAddress(
    uint64_t address) const {
  if (auto it = kernel_modules_.Find(address); it != kernel_modules_.end()) {
    return it->second;
  }
  if (kernel_ && kernel_->memory_range().Contains(address)) {
    return kernel_;
  }
  return nullptr;
}

UserMemoryMapping* MappingTracker::FindUserMappingForAddress(
    UniquePid upid,
    uint64_t address) const {
  if (auto* vm = user_memory_.Find(upid); vm) {
    if (auto it = vm->Find(address); it != vm->end()) {
      return it->second;
    }
  }

  if (auto* delegates = jit_caches_.Find(upid); delegates) {
    if (auto it = delegates->Find(address); it != delegates->end()) {
      return &it->second->CreateMapping();
    }
  }

  return nullptr;
}

std::vector<VirtualMemoryMapping*> MappingTracker::FindMappings(
    base::StringView name,
    const BuildId& build_id) const {
  if (auto res = mappings_by_name_and_build_id_.Find({name, build_id});
      res != nullptr) {
    return *res;
  }
  return {};
}

VirtualMemoryMapping& MappingTracker::InternMemoryMapping(
    CreateMappingParams params) {
  if (auto* mapping = interned_mappings_.Find(params); mapping) {
    return **mapping;
  }

  std::unique_ptr<VirtualMemoryMapping> mapping(
      new VirtualMemoryMapping(context_, params));
  interned_mappings_.Insert(std::move(params), mapping.get());
  return AddMapping(std::move(mapping));
}

void MappingTracker::AddJitRange(UniquePid upid,
                                 AddressRange jit_range,
                                 JitCache* jit_cache) {
  // TODO(carlscab): Deal with overlaps
  jit_caches_[upid].TrimOverlapsAndEmplace(jit_range, jit_cache);
  user_memory_[upid].ForOverlaps(
      jit_range, [&](std::pair<const AddressRange, UserMemoryMapping*>& entry) {
        PERFETTO_CHECK(jit_range.Contains(entry.first));
        entry.second->SetJitCache(jit_cache);
      });
}

DummyMemoryMapping& MappingTracker::CreateDummyMapping(std::string name) {
  CreateMappingParams params;
  params.name = std::move(name);
  params.memory_range =
      AddressRange::FromStartAndSize(0, std::numeric_limits<uint64_t>::max());
  std::unique_ptr<DummyMemoryMapping> mapping(
      new DummyMemoryMapping(context_, std::move(params)));

  return AddMapping(std::move(mapping));
}

}  // namespace trace_processor
}  // namespace perfetto
