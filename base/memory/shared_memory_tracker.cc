// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/shared_memory_tracker.h"

#include "base/memory/shared_memory.h"
#include "base/strings/string_number_conversions.h"
#include "base/trace_event/memory_allocator_dump_guid.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/process_memory_dump.h"

namespace base {

const char SharedMemoryTracker::kDumpRootName[] = "shared_memory";

// static
SharedMemoryTracker* SharedMemoryTracker::GetInstance() {
  static SharedMemoryTracker* instance = new SharedMemoryTracker;
  return instance;
}

// static
std::string SharedMemoryTracker::GetDumpNameForTracing(
    const UnguessableToken& id) {
  DCHECK(!id.is_empty());
  return std::string(kDumpRootName) + "/" + id.ToString();
}

// static
trace_event::MemoryAllocatorDumpGuid
SharedMemoryTracker::GetGlobalDumpIdForTracing(const UnguessableToken& id) {
  std::string dump_name = GetDumpNameForTracing(id);
  return trace_event::MemoryAllocatorDumpGuid(dump_name);
}

// static
const trace_event::MemoryAllocatorDump*
SharedMemoryTracker::GetOrCreateSharedMemoryDump(
    const SharedMemory* shared_memory,
    trace_event::ProcessMemoryDump* pmd) {
  const std::string dump_name =
      GetDumpNameForTracing(shared_memory->mapped_id());
  trace_event::MemoryAllocatorDump* local_dump =
      pmd->GetAllocatorDump(dump_name);
  if (local_dump)
    return local_dump;

  size_t virtual_size = shared_memory->mapped_size();
  // If resident size is not available, a virtual size is used as fallback.
  size_t size = virtual_size;
#if defined(COUNT_RESIDENT_BYTES_SUPPORTED)
  base::Optional<size_t> resident_size =
      trace_event::ProcessMemoryDump::CountResidentBytesInSharedMemory(
          *shared_memory);
  if (resident_size.has_value())
    size = resident_size.value();
#endif

  local_dump = pmd->CreateAllocatorDump(dump_name);
  local_dump->AddScalar(trace_event::MemoryAllocatorDump::kNameSize,
                        trace_event::MemoryAllocatorDump::kUnitsBytes, size);
  local_dump->AddScalar("virtual_size",
                        trace_event::MemoryAllocatorDump::kUnitsBytes,
                        virtual_size);
  auto global_dump_guid = GetGlobalDumpIdForTracing(shared_memory->mapped_id());
  trace_event::MemoryAllocatorDump* global_dump =
      pmd->CreateSharedGlobalAllocatorDump(global_dump_guid);
  global_dump->AddScalar(trace_event::MemoryAllocatorDump::kNameSize,
                         trace_event::MemoryAllocatorDump::kUnitsBytes, size);

  // The edges will be overriden by the clients with correct importance.
  pmd->AddOverridableOwnershipEdge(local_dump->guid(), global_dump->guid(),
                                   0 /* importance */);
  return local_dump;
}

void SharedMemoryTracker::IncrementMemoryUsage(
    const SharedMemory& shared_memory) {
  AutoLock hold(usages_lock_);
  DCHECK(usages_.find(&shared_memory) == usages_.end());
  usages_[&shared_memory] = shared_memory.mapped_size();
}

void SharedMemoryTracker::DecrementMemoryUsage(
    const SharedMemory& shared_memory) {
  AutoLock hold(usages_lock_);
  DCHECK(usages_.find(&shared_memory) != usages_.end());
  usages_.erase(&shared_memory);
}

bool SharedMemoryTracker::OnMemoryDump(const trace_event::MemoryDumpArgs& args,
                                       trace_event::ProcessMemoryDump* pmd) {
  {
    AutoLock hold(usages_lock_);
    for (const auto& usage : usages_) {
      const trace_event::MemoryAllocatorDump* dump =
          GetOrCreateSharedMemoryDump(usage.first, pmd);
      DCHECK(dump);
    }
  }
  return true;
}

SharedMemoryTracker::SharedMemoryTracker() {
  trace_event::MemoryDumpManager::GetInstance()->RegisterDumpProvider(
      this, "SharedMemoryTracker", nullptr);
}

SharedMemoryTracker::~SharedMemoryTracker() = default;

}  // namespace
