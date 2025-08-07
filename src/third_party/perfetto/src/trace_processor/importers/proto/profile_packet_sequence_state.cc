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

#include "src/trace_processor/importers/proto/profile_packet_sequence_state.h"

#include "perfetto/base/flat_set.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/profile_packet_utils.h"
#include "src/trace_processor/importers/proto/stack_profile_sequence_state.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/build_id.h"

namespace perfetto {
namespace trace_processor {
namespace {
const char kArtHeapName[] = "com.android.art";
}

ProfilePacketSequenceState::ProfilePacketSequenceState(
    TraceProcessorContext* context)
    : context_(context) {
  strings_.Insert(0, "");
}

ProfilePacketSequenceState::~ProfilePacketSequenceState() = default;

void ProfilePacketSequenceState::SetProfilePacketIndex(uint64_t index) {
  bool dropped_packet = false;
  // heapprofd starts counting at index = 0.
  if (!prev_index.has_value() && index != 0) {
    dropped_packet = true;
  }

  if (prev_index.has_value() && *prev_index + 1 != index) {
    dropped_packet = true;
  }

  if (dropped_packet) {
    context_->storage->IncrementStats(stats::heapprofd_missing_packet);
  }
  prev_index = index;
}

void ProfilePacketSequenceState::AddString(SourceStringId id,
                                           base::StringView str) {
  PERFETTO_CHECK(id != 0 || str.empty());
  strings_.Insert(id, str.ToStdString());
}

void ProfilePacketSequenceState::AddMapping(SourceMappingId id,
                                            const SourceMapping& mapping) {
  CreateMappingParams params;
  if (std::string* str = strings_.Find(mapping.build_id); str) {
    params.build_id = BuildId::FromRaw(*str);
  } else {
    context_->storage->IncrementStats(stats::stackprofile_invalid_string_id);
    return;
  }
  params.exact_offset = mapping.exact_offset;
  params.start_offset = mapping.start_offset;
  params.memory_range = AddressRange(mapping.start, mapping.end);
  params.load_bias = mapping.load_bias;

  std::vector<base::StringView> path_components;
  path_components.reserve(mapping.name_ids.size());
  for (SourceStringId string_id : mapping.name_ids) {
    if (std::string* str = strings_.Find(string_id); str) {
      path_components.push_back(base::StringView(*str));
    } else {
      context_->storage->IncrementStats(stats::stackprofile_invalid_string_id);
      // For backward compatibility reasons we do not return an error but
      // instead stop adding path components.
      break;
    }
  }

  params.name = ProfilePacketUtils::MakeMappingName(path_components);
  mappings_.Insert(
      id, &context_->mapping_tracker->InternMemoryMapping(std::move(params)));
}

void ProfilePacketSequenceState::AddFrame(SourceFrameId id,
                                          const SourceFrame& frame) {
  VirtualMemoryMapping* mapping;
  if (auto* ptr = mappings_.Find(frame.mapping_id); ptr) {
    mapping = *ptr;
  } else {
    context_->storage->IncrementStats(stats::stackprofile_invalid_mapping_id);
    return;
  }

  std::string* function_name = strings_.Find(frame.name_id);
  if (!function_name) {
    context_->storage->IncrementStats(stats::stackprofile_invalid_string_id);
    return;
  }

  FrameId frame_id =
      mapping->InternFrame(frame.rel_pc, base::StringView(*function_name));
  PERFETTO_CHECK(!mapping->is_jitted());
  frames_.Insert(id, frame_id);
}

void ProfilePacketSequenceState::AddCallstack(
    SourceCallstackId id,
    const SourceCallstack& callstack) {
  std::optional<CallsiteId> parent_callsite_id;
  uint32_t depth = 0;
  for (SourceFrameId source_frame_id : callstack) {
    FrameId* frame_id = frames_.Find(source_frame_id);
    if (!frame_id) {
      context_->storage->IncrementStats(stats::stackprofile_invalid_frame_id);
      return;
    }
    parent_callsite_id = context_->stack_profile_tracker->InternCallsite(
        parent_callsite_id, *frame_id, depth);
    ++depth;
  }

  if (!parent_callsite_id) {
    context_->storage->IncrementStats(stats::stackprofile_empty_callstack);
    return;
  }

  callstacks_.Insert(id, *parent_callsite_id);
}

void ProfilePacketSequenceState::StoreAllocation(
    const SourceAllocation& alloc) {
  pending_allocs_.push_back(std::move(alloc));
}

void ProfilePacketSequenceState::CommitAllocations() {
  for (const SourceAllocation& alloc : pending_allocs_)
    AddAllocation(alloc);
  pending_allocs_.clear();
}

void ProfilePacketSequenceState::FinalizeProfile() {
  CommitAllocations();
  strings_.Clear();
  mappings_.Clear();
  frames_.Clear();
  callstacks_.Clear();
}

FrameId ProfilePacketSequenceState::GetDatabaseFrameIdForTesting(
    SourceFrameId source_frame_id) {
  FrameId* frame_id = frames_.Find(source_frame_id);
  if (!frame_id) {
    PERFETTO_DLOG("Invalid frame.");
    return {};
  }
  return *frame_id;
}

void ProfilePacketSequenceState::AddAllocation(const SourceAllocation& alloc) {
  const UniquePid upid = context_->process_tracker->GetOrCreateProcess(
      static_cast<uint32_t>(alloc.pid));
  auto opt_callstack_id = FindOrInsertCallstack(upid, alloc.callstack_id);
  if (!opt_callstack_id)
    return;

  CallsiteId callstack_id = *opt_callstack_id;

  tables::HeapProfileAllocationTable::Row alloc_row{
      alloc.timestamp,
      upid,
      alloc.heap_name,
      callstack_id,
      static_cast<int64_t>(alloc.alloc_count),
      static_cast<int64_t>(alloc.self_allocated)};

  tables::HeapProfileAllocationTable::Row free_row{
      alloc.timestamp,
      upid,
      alloc.heap_name,
      callstack_id,
      -static_cast<int64_t>(alloc.free_count),
      -static_cast<int64_t>(alloc.self_freed)};

  auto* prev_alloc = prev_alloc_.Find({upid, callstack_id});
  if (!prev_alloc) {
    prev_alloc = prev_alloc_
                     .Insert(std::make_pair(upid, callstack_id),
                             tables::HeapProfileAllocationTable::Row{})
                     .first;
  }

  auto* prev_free = prev_free_.Find({upid, callstack_id});
  if (!prev_free) {
    prev_free = prev_free_
                    .Insert(std::make_pair(upid, callstack_id),
                            tables::HeapProfileAllocationTable::Row{})
                    .first;
  }

  base::FlatSet<CallsiteId>& callstacks_for_source_callstack_id =
      seen_callstacks_[SourceAllocationIndex{upid, alloc.callstack_id,
                                             alloc.heap_name}];
  bool new_callstack;
  std::tie(std::ignore, new_callstack) =
      callstacks_for_source_callstack_id.insert(callstack_id);

  if (new_callstack) {
    alloc_correction_[alloc.callstack_id] = *prev_alloc;
    free_correction_[alloc.callstack_id] = *prev_free;
  }

  const auto* alloc_correction = alloc_correction_.Find(alloc.callstack_id);
  if (alloc_correction) {
    alloc_row.count += alloc_correction->count;
    alloc_row.size += alloc_correction->size;
  }

  const auto* free_correction = free_correction_.Find(alloc.callstack_id);
  if (free_correction) {
    free_row.count += free_correction->count;
    free_row.size += free_correction->size;
  }

  tables::HeapProfileAllocationTable::Row alloc_delta = alloc_row;
  tables::HeapProfileAllocationTable::Row free_delta = free_row;

  alloc_delta.count -= prev_alloc->count;
  alloc_delta.size -= prev_alloc->size;

  free_delta.count -= prev_free->count;
  free_delta.size -= prev_free->size;

  if (alloc_delta.count < 0 || alloc_delta.size < 0 || free_delta.count > 0 ||
      free_delta.size > 0) {
    PERFETTO_DLOG("Non-monotonous allocation.");
    context_->storage->IncrementIndexedStats(stats::heapprofd_malformed_packet,
                                             static_cast<int>(upid));
    return;
  }

  // Dump at max profiles do not have .count set.
  if (alloc_delta.count || alloc_delta.size) {
    context_->storage->mutable_heap_profile_allocation_table()->Insert(
        alloc_delta);
  }

  // ART only reports allocations, and not frees. This throws off our logic
  // that assumes that if a new object was allocated with the same address,
  // the old one has to have been freed in the meantime.
  // See HeapTracker::RecordMalloc in bookkeeping.cc.
  if (context_->storage->GetString(alloc.heap_name) != kArtHeapName &&
      (free_delta.count || free_delta.size)) {
    context_->storage->mutable_heap_profile_allocation_table()->Insert(
        free_delta);
  }

  *prev_alloc = alloc_row;
  *prev_free = free_row;
}

std::optional<CallsiteId> ProfilePacketSequenceState::FindOrInsertCallstack(
    UniquePid upid,
    uint64_t iid) {
  if (CallsiteId* id = callstacks_.Find(iid); id) {
    return *id;
  }
  return GetCustomState<StackProfileSequenceState>()->FindOrInsertCallstack(
      upid, iid);
}

}  // namespace trace_processor
}  // namespace perfetto
