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

#include "src/trace_processor/sorter/trace_token_buffer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/util/bump_allocator.h"

namespace perfetto::trace_processor {
namespace {

struct alignas(8) TrackEventDataDescriptor {
  static constexpr uint8_t kMaxOffsetFromInternedBlobBits = 25;
  static constexpr uint32_t kMaxOffsetFromInternedBlob =
      (1Ul << kMaxOffsetFromInternedBlobBits) - 1;

  static constexpr uint8_t kMaxExtraCountersBits = 4;
  static constexpr uint8_t kMaxExtraCounters = (1 << kMaxExtraCountersBits) - 1;
  static_assert(TrackEventData::kMaxNumExtraCounters <= kMaxExtraCounters,
                "Counter bits must be able to fit TrackEventData counters");

  uint16_t intern_blob_index;
  uint16_t intern_seq_index;
  uint32_t intern_blob_offset : kMaxOffsetFromInternedBlobBits;
  uint32_t has_thread_timestamp : 1;
  uint32_t has_thread_instruction_count : 1;
  uint32_t has_counter_value : 1;
  uint32_t extra_counter_count : kMaxExtraCountersBits;
};
static_assert(sizeof(TrackEventDataDescriptor) == 8,
              "CompressedTracePacketData must be small");
static_assert(alignof(TrackEventDataDescriptor) == 8,
              "CompressedTracePacketData must be 8-aligned");

template <typename T>
T ExtractFromPtr(uint8_t** ptr) {
  T* typed_ptr = reinterpret_cast<T*>(*ptr);
  T value(std::move(*typed_ptr));
  typed_ptr->~T();
  *ptr += sizeof(T);
  return value;
}

template <typename T>
uint8_t* AppendToPtr(uint8_t* ptr, T value) {
  new (ptr) T(std::move(value));
  return ptr + sizeof(T);
}

uint32_t GetAllocSize(const TrackEventDataDescriptor& desc) {
  uint32_t alloc_size = sizeof(TrackEventDataDescriptor);
  alloc_size += sizeof(uint64_t);
  alloc_size += desc.has_thread_instruction_count * sizeof(int64_t);
  alloc_size += desc.has_thread_timestamp * sizeof(int64_t);
  alloc_size += desc.has_counter_value * sizeof(double);
  alloc_size += desc.extra_counter_count * sizeof(double);
  return alloc_size;
}

}  // namespace

TraceTokenBuffer::Id TraceTokenBuffer::Append(TrackEventData ted) {
  // TrackEventData (and TracePacketData) are two big contributors to the size
  // of the peak memory usage by sorted. The main reasons for this are a) object
  // padding and b) using more bits than necessary to store their contents.
  //
  // The purpose of this function is to "compress" the contents of
  // TrackEventData by utilising techniques like bitpacking, interning and
  // variable length encoding to ensure only the amount of data which really
  // needs to be stored is done so.

  // Compress all the booleans indicating the presence of a value into 4 bits
  // instead of 4 bytes as they would take inside base::Optional.
  TrackEventDataDescriptor desc;
  desc.has_thread_instruction_count = ted.thread_instruction_count.has_value();
  desc.has_thread_timestamp = ted.thread_timestamp.has_value();
  desc.has_counter_value = std::not_equal_to<double>()(ted.counter_value, 0);
  desc.extra_counter_count = ted.CountExtraCounterValues();

  // Allocate enough memory using the BumpAllocator to store the data in |ted|.
  // Also figure out the interned index.
  BumpAllocator::AllocId alloc_id =
      AllocAndResizeInternedVectors(GetAllocSize(desc));
  InternedIndex interned_index = GetInternedIndex(alloc_id);

  // Compute the interning information for the TrackBlob and the SequenceState.
  TracePacketData& tpd = ted.trace_packet_data;
  desc.intern_blob_offset = InternTraceBlob(interned_index, tpd.packet);
  desc.intern_blob_index =
      static_cast<uint16_t>(interned_blobs_.at(interned_index).size() - 1);
  desc.intern_seq_index =
      InternSeqState(interned_index, std::move(tpd.sequence_state));

  // Store the descriptor
  uint8_t* ptr = static_cast<uint8_t*>(allocator_.GetPointer(alloc_id));
  ptr = AppendToPtr(ptr, desc);

  // Store the packet sizes.
  uint64_t packet_size = static_cast<uint64_t>(tpd.packet.size());
  ptr = AppendToPtr(ptr, packet_size);

  // Add the "optional" fields of TrackEventData based on whether or not they
  // are non-null.
  if (desc.has_thread_instruction_count) {
    ptr = AppendToPtr(ptr, ted.thread_instruction_count.value());
  }
  if (desc.has_thread_timestamp) {
    ptr = AppendToPtr(ptr, ted.thread_timestamp.value());
  }
  if (desc.has_counter_value) {
    ptr = AppendToPtr(ptr, ted.counter_value);
  }
  for (uint32_t i = 0; i < desc.extra_counter_count; ++i) {
    ptr = AppendToPtr(ptr, ted.extra_counter_values[i]);
  }
  return Id{alloc_id};
}

template <>
TrackEventData TraceTokenBuffer::Extract<TrackEventData>(Id id) {
  uint8_t* ptr = static_cast<uint8_t*>(allocator_.GetPointer(id.alloc_id));
  TrackEventDataDescriptor desc =
      ExtractFromPtr<TrackEventDataDescriptor>(&ptr);
  uint64_t packet_size = ExtractFromPtr<uint64_t>(&ptr);

  InternedIndex interned_index = GetInternedIndex(id.alloc_id);
  BlobWithOffset& bwo =
      interned_blobs_.at(interned_index)[desc.intern_blob_index];
  TraceBlobView tbv(RefPtr<TraceBlob>::FromReleasedUnsafe(bwo.blob),
                    bwo.offset_in_blob + desc.intern_blob_offset,
                    static_cast<uint32_t>(packet_size));
  auto seq = RefPtr<PacketSequenceStateGeneration>::FromReleasedUnsafe(
      interned_seqs_.at(interned_index)[desc.intern_seq_index]);

  TrackEventData ted{std::move(tbv), std::move(seq)};
  if (desc.has_thread_instruction_count) {
    ted.thread_instruction_count = ExtractFromPtr<int64_t>(&ptr);
  }
  if (desc.has_thread_timestamp) {
    ted.thread_timestamp = ExtractFromPtr<int64_t>(&ptr);
  }
  if (desc.has_counter_value) {
    ted.counter_value = ExtractFromPtr<double>(&ptr);
  }
  for (uint32_t i = 0; i < desc.extra_counter_count; ++i) {
    ted.extra_counter_values[i] = ExtractFromPtr<double>(&ptr);
  }
  allocator_.Free(id.alloc_id);
  return ted;
}

uint32_t TraceTokenBuffer::InternTraceBlob(InternedIndex interned_index,
                                           const TraceBlobView& tbv) {
  BlobWithOffsets& blobs = interned_blobs_.at(interned_index);
  if (blobs.empty()) {
    return AddTraceBlob(interned_index, tbv);
  }

  BlobWithOffset& last_blob = blobs.back();
  if (last_blob.blob != tbv.blob().get()) {
    return AddTraceBlob(interned_index, tbv);
  }
  PERFETTO_CHECK(last_blob.offset_in_blob <= tbv.offset());

  // To allow our offsets in the store to be 16 bits, we intern not only the
  // TraceBlob pointer but also the offset. By having this double indirection,
  // we can store offset always as uint16 at the cost of storing blobs here more
  // often: this more than pays for itself as in the majority of cases the
  // offsets are small anyway.
  size_t rel_offset = tbv.offset() - last_blob.offset_in_blob;
  if (rel_offset > TrackEventDataDescriptor::kMaxOffsetFromInternedBlob) {
    return AddTraceBlob(interned_index, tbv);
  }

  // Intentionally "leak" this pointer. This essentially keeps the refcount
  // of this TraceBlob one higher than the number of RefPtrs pointing to it.
  // This allows avoid storing the same RefPtr n times.
  //
  // Calls to this function are paired to Extract<TrackEventData> which picks
  // up this "leaked" pointer.
  TraceBlob* leaked = tbv.blob().ReleaseUnsafe();
  base::ignore_result(leaked);
  return static_cast<uint32_t>(rel_offset);
}

uint16_t TraceTokenBuffer::InternSeqState(
    InternedIndex interned_index,
    RefPtr<PacketSequenceStateGeneration> ptr) {
  // Look back at most 32 elements. This should be far enough in most cases
  // unless either: a) we are essentially round-robining between >32 sequences
  // b) we are churning through generations. Either case seems pathological.
  SequenceStates& states = interned_seqs_.at(interned_index);
  size_t lookback = std::min<size_t>(32u, states.size());
  for (uint32_t i = 0; i < lookback; ++i) {
    uint16_t idx = static_cast<uint16_t>(states.size() - 1 - i);
    if (states[idx] == ptr.get()) {
      // Intentionally "leak" this pointer. See |InternTraceBlob| for an
      // explanation.
      PacketSequenceStateGeneration* leaked = ptr.ReleaseUnsafe();
      base::ignore_result(leaked);
      return idx;
    }
  }
  states.emplace_back(ptr.ReleaseUnsafe());
  PERFETTO_CHECK(states.size() <= std::numeric_limits<uint16_t>::max());
  return static_cast<uint16_t>(states.size() - 1);
}

uint32_t TraceTokenBuffer::AddTraceBlob(InternedIndex interned_index,
                                        const TraceBlobView& tbv) {
  BlobWithOffsets& blobs = interned_blobs_.at(interned_index);
  blobs.emplace_back(BlobWithOffset{tbv.blob().ReleaseUnsafe(), tbv.offset()});
  PERFETTO_CHECK(blobs.size() <= std::numeric_limits<uint16_t>::max());
  return 0u;
}

void TraceTokenBuffer::FreeMemory() {
  uint64_t erased = allocator_.EraseFrontFreeChunks();
  PERFETTO_CHECK(erased <= std::numeric_limits<size_t>::max());
  interned_blobs_.erase_front(static_cast<size_t>(erased));
  interned_seqs_.erase_front(static_cast<size_t>(erased));
  PERFETTO_CHECK(interned_blobs_.size() == interned_seqs_.size());
}

BumpAllocator::AllocId TraceTokenBuffer::AllocAndResizeInternedVectors(
    uint32_t size) {
  uint64_t erased = allocator_.erased_front_chunks_count();
  BumpAllocator::AllocId alloc_id = allocator_.Alloc(size);
  uint64_t allocator_chunks_size = alloc_id.chunk_index - erased + 1;

  // The allocator should never "remove" chunks from being tracked.
  PERFETTO_DCHECK(allocator_chunks_size >= interned_blobs_.size());

  // We should add at most one chunk in the allocator.
  uint64_t chunks_added = allocator_chunks_size - interned_blobs_.size();
  PERFETTO_DCHECK(chunks_added <= 1);
  PERFETTO_DCHECK(interned_blobs_.size() == interned_seqs_.size());
  for (uint64_t i = 0; i < chunks_added; ++i) {
    interned_blobs_.emplace_back();
    interned_seqs_.emplace_back();
  }
  return alloc_id;
}

TraceTokenBuffer::InternedIndex TraceTokenBuffer::GetInternedIndex(
    BumpAllocator::AllocId alloc_id) {
  uint64_t interned_index =
      alloc_id.chunk_index - allocator_.erased_front_chunks_count();
  PERFETTO_DCHECK(interned_index <= std::numeric_limits<size_t>::max());
  PERFETTO_DCHECK(interned_index < interned_blobs_.size());
  PERFETTO_DCHECK(interned_index < interned_seqs_.size());
  PERFETTO_DCHECK(interned_blobs_.size() == interned_seqs_.size());
  return static_cast<size_t>(interned_index);
}

}  // namespace perfetto::trace_processor
