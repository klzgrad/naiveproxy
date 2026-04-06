/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/util/bump_allocator.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/utils.h"

namespace perfetto::trace_processor {
namespace {

// TODO(b/266983484): consider using base::PagedMemory unless a) we are on a
// platform where that doesn't make sense (WASM) b) we are trying to do heap
// profiling.
base::AlignedUniquePtr<uint8_t[]> Allocate(uint32_t size) {
  uint8_t* ptr = static_cast<uint8_t*>(base::AlignedAlloc(8, size));
  // Poison the region to try and catch out of bound accesses.
  PERFETTO_ASAN_POISON(ptr, size);
  return base::AlignedUniquePtr<uint8_t[]>(ptr);
}

}  // namespace

BumpAllocator::BumpAllocator() = default;

BumpAllocator::~BumpAllocator() {
  for (const auto& chunk : chunks_) {
    PERFETTO_CHECK(chunk.unfreed_allocations == 0);
  }
}

BumpAllocator::AllocId BumpAllocator::Alloc(uint32_t size) {
  // Size is required to be a multiple of 8 to avoid needing to deal with
  // alignment. It must also be at most kChunkSize as we do not support cross
  // chunk spanning allocations.
  PERFETTO_DCHECK(size % 8 == 0);
  PERFETTO_DCHECK(size <= kChunkSize);

  // Fast path: check if we have space to service this allocation in the current
  // chunk.
  std::optional<AllocId> alloc_id = TryAllocInLastChunk(size);
  if (alloc_id) {
    return *alloc_id;
  }

  // Slow path: we don't have enough space in the last chunk so we create one.
  Chunk chunk;
  chunk.allocation = Allocate(kChunkSize);
  chunks_.emplace_back(std::move(chunk));

  // Ensure that we haven't exceeded the maximum number of chunks.
  PERFETTO_CHECK(LastChunkIndex() < kMaxChunkCount);

  // This time the allocation should definitely succeed in the last chunk (which
  // we just added).
  alloc_id = TryAllocInLastChunk(size);
  PERFETTO_CHECK(alloc_id);
  return *alloc_id;
}

void BumpAllocator::Free(AllocId id) {
  uint64_t queue_index = ChunkIndexToQueueIndex(id.chunk_index);
  PERFETTO_DCHECK(queue_index <= std::numeric_limits<size_t>::max());
  Chunk& chunk = chunks_.at(static_cast<size_t>(queue_index));
  PERFETTO_DCHECK(chunk.unfreed_allocations > 0);
  chunk.unfreed_allocations--;
}

void* BumpAllocator::GetPointer(AllocId id) {
  uint64_t queue_index = ChunkIndexToQueueIndex(id.chunk_index);
  PERFETTO_CHECK(queue_index <= std::numeric_limits<size_t>::max());
  return chunks_.at(static_cast<size_t>(queue_index)).allocation.get() +
         id.chunk_offset;
}

uint64_t BumpAllocator::EraseFrontFreeChunks() {
  size_t to_erase_chunks = 0;
  for (; to_erase_chunks < chunks_.size(); ++to_erase_chunks) {
    // Break on the first chunk which still has unfreed allocations.
    if (chunks_.at(to_erase_chunks).unfreed_allocations > 0) {
      break;
    }
  }
  chunks_.erase_front(to_erase_chunks);
  erased_front_chunks_count_ += to_erase_chunks;
  return to_erase_chunks;
}

BumpAllocator::AllocId BumpAllocator::PastTheEndId() {
  if (chunks_.empty()) {
    return AllocId{erased_front_chunks_count_, 0};
  }
  if (chunks_.back().bump_offset == kChunkSize) {
    return AllocId{LastChunkIndex() + 1, 0};
  }
  return AllocId{LastChunkIndex(), chunks_.back().bump_offset};
}

std::optional<BumpAllocator::AllocId> BumpAllocator::TryAllocInLastChunk(
    uint32_t size) {
  if (chunks_.empty()) {
    return std::nullopt;
  }

  // TODO(266983484): consider switching this to bump downwards instead of
  // upwards for more efficient code generation.
  Chunk& chunk = chunks_.back();

  // Verify some invariants:
  // 1) The allocation must exist
  // 2) The bump must be in the bounds of the chunk.
  PERFETTO_DCHECK(chunk.allocation);
  PERFETTO_DCHECK(chunk.bump_offset <= kChunkSize);

  // If the end of the allocation ends up after this chunk, we cannot service it
  // in this chunk.
  uint32_t alloc_offset = chunk.bump_offset;
  uint32_t new_bump_offset = chunk.bump_offset + size;
  if (new_bump_offset > kChunkSize) {
    return std::nullopt;
  }

  // Set the new offset equal to the end of this allocation and increment the
  // unfreed allocation counter.
  chunk.bump_offset = new_bump_offset;
  chunk.unfreed_allocations++;

  // Unpoison the allocation range to allow access to it on ASAN builds.
  PERFETTO_ASAN_UNPOISON(chunk.allocation.get() + alloc_offset, size);

  return AllocId{LastChunkIndex(), alloc_offset};
}

}  // namespace perfetto::trace_processor
