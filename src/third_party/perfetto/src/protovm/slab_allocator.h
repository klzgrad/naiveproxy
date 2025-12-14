/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_PROTOVM_SLAB_ALLOCATOR_H_
#define SRC_PROTOVM_SLAB_ALLOCATOR_H_

#include <cstdlib>
#include <new>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/paged_memory.h"
#include "src/base/intrusive_list.h"

// An efficient allocator for elements with fixed size and alignment
// requirements.
//
// Design doc: go/perfetto-protovm-implementation
//
// Key features:
//
// - Slab allocation: Instead of requesting memory for each individual element,
//   this allocator pre-allocates large memory chunks (slabs). Each slab is
//   designed to hold multiple elements.
//
// - Element free list: A free list tracks available elements within each
//   individual slab, allowing for O(1) access time during allocation.
//
// - Slab intrusive lists: Slabs are managed within one of two intrusive lists.
//   The "non-full slabs" list and the "full slabs" list. This organization
//   allows "non-full" slabs (those with available space for new allocations) to
//   be accessed in O(1) time.
//
// - Block-to-Slab hash map: A hash map links 4KB-aligned memory blocks to their
//   corresponding slab. This enables O(1) mapping of an element back to its
//   slab during deallocation.
//
// Allocation process:
// 1. If there is no free slab
//    1.1 Allocate a new slab, add it to the “non-full slabs” list, populate the
//        block-to-slab map
// 2. Pick any slab from the “non-full slabs” list
// 3. Allocate the element
// 4. If needed, move the slab to the “full slabs” list
//
// Deallocation process:
// 1. Find slab from the block-to-slab map
// 2. Free element
// 3. If needed, move to the “non-full slabs” list
// 4. If needed, deallocate the slab.

namespace perfetto {
namespace protovm {

namespace internal {
static constexpr size_t k4KiloBytes = static_cast<size_t>(4096);
}

template <size_t ElementSize, size_t ElementAlign, size_t Blocks4KB>
class Slab {
 public:
  struct IntrusiveListTraits {
    static constexpr size_t node_offset() {
      return offsetof(Slab, intrusive_list_node_);
    }
  };

  Slab() : paged_memory_(base::PagedMemory::Allocate(ElementSize * kCapacity)) {
    PERFETTO_CHECK(paged_memory_.IsValid());

    // Expect allocated memory to be always 4KB-aligned
    PERFETTO_CHECK(reinterpret_cast<uintptr_t>(paged_memory_.Get()) %
                       internal::k4KiloBytes ==
                   0);

    ConstructSlots();
    InitializeSlotsFreeList();
  }

  ~Slab() { DestroySlots(); }

  void* Allocate() {
    PERFETTO_DCHECK(next_free_slot_);
    auto* slot = next_free_slot_;
    next_free_slot_ = next_free_slot_->next;
    ++size_;
    return slot;
  }

  void Free(void* p) {
    auto* slot = static_cast<Slot*>(p);
    PERFETTO_DCHECK(slot >= slots() && slot < slots() + kCapacity);
    slot->next = next_free_slot_;
    next_free_slot_ = slot;
    --size_;
  }

  bool IsFull() const { return size_ == kCapacity; }

  bool IsEmpty() const { return size_ == 0; }

  const void* GetBeginAddress() const {
    return const_cast<Slab*>(this)->slots();
  }

  const void* GetEndAddress() const {
    return const_cast<Slab*>(this)->slots() + kCapacity;
  }

 private:
  static constexpr size_t kCapacity =
      Blocks4KB * (internal::k4KiloBytes / ElementSize);

  static_assert(kCapacity >= 128,
                "The configured number of 4KB blocks per slab seems too small, "
                "resulting in a low slab capacity. Slab allocation is "
                "expensive (involves syscalls), so a high elements-to-slab "
                "ratio is desirable to amortize the cost.");

  static_assert(ElementAlign <= internal::k4KiloBytes,
                "SlabAllocator currently supports alignment <= 4KB");

  union Slot {
    Slot* next;
    alignas(ElementAlign) unsigned char element[ElementSize];
  };

  void InitializeSlotsFreeList() {
    next_free_slot_ = &slots()[0];

    for (size_t i = 0; i + 1 < kCapacity; ++i) {
      auto& slot = slots()[i];
      auto& next_slot = slots()[i + 1];
      slot.next = &next_slot;
    }

    auto& last_slot = slots()[kCapacity - 1];
    last_slot.next = nullptr;
  }

  void ConstructSlots() {
    auto* slot = slots();
    for (size_t i = 0; i < kCapacity; ++i) {
      new (&slot[i]) Slot();
    }
  }

  void DestroySlots() {
    auto* slot = slots();
    for (size_t i = 0; i < kCapacity; ++i) {
      slot[i].~Slot();
    }
  }

  Slot* slots() {
    return std::launder(reinterpret_cast<Slot*>(paged_memory_.Get()));
  }

  Slot* next_free_slot_{nullptr};
  size_t size_{0};
  base::IntrusiveListNode intrusive_list_node_;
  base::PagedMemory paged_memory_;
};

template <size_t ElementSize, size_t ElementAlign, size_t Blocks4KBPerSlab = 16>
class SlabAllocator {
 public:
  ~SlabAllocator() {
    DeleteSlabs(slabs_non_full_);
    DeleteSlabs(slabs_full_);
  }

  void* Allocate() {
    // Create new slab if needed
    if (slabs_non_full_.empty()) {
      auto* slab = new SlabType();
      slabs_non_full_.PushFront(*slab);
      ++slabs_non_full_size_;
      InsertHashMapEntries(*slab);
    }

    // Allocate using any non-full slab
    auto& slab = slabs_non_full_.front();
    auto* allocated = slab.Allocate();
    PERFETTO_CHECK(allocated);

    // Move to "full slabs" list if needed
    if (slab.IsFull()) {
      slabs_non_full_.Erase(slab);
      --slabs_non_full_size_;
      slabs_full_.PushFront(slab);
    }

    return allocated;
  }

  void Free(void* p) {
    auto& slab = FindSlabInHashMap(p);

    // Move to "non-full slabs" list if needed
    if (slab.IsFull()) {
      slabs_full_.Erase(slab);
      slabs_non_full_.PushFront(slab);
      ++slabs_non_full_size_;
    }

    slab.Free(p);

    // Deallocate the slab if it becomes empty and it's not the sole non-full
    // slab.
    //
    // The "is not the sole non-full slab" condition avoids thrashing scenarios
    // where a slab is repeatedly allocated and deallocated. For example:
    // 1. Allocate element x -> a new slab is allocated.
    // 2. Free element x -> slab becomes empty and is deallocated.
    // 3. Allocate element y -> a new slab is allocated again.
    // 4. And so on...
    if (slab.IsEmpty() && slabs_non_full_size_ > 1) {
      EraseHashMapEntries(slab);
      slabs_non_full_.Erase(slab);
      --slabs_non_full_size_;
      delete &slab;
    }
  }

 private:
  using SlabType = Slab<ElementSize, ElementAlign, Blocks4KBPerSlab>;
  using SlabList =
      base::IntrusiveList<SlabType, typename SlabType::IntrusiveListTraits>;

  void InsertHashMapEntries(SlabType& slab) {
    for (auto p = reinterpret_cast<uintptr_t>(slab.GetBeginAddress());
         p < reinterpret_cast<uintptr_t>(slab.GetEndAddress());
         p += internal::k4KiloBytes) {
      PERFETTO_DCHECK(p % internal::k4KiloBytes == 0);
      block_4KB_aligned_to_slab_.Insert(p, &slab);
    }
  }

  void EraseHashMapEntries(const SlabType& slab) {
    for (auto p = reinterpret_cast<uintptr_t>(slab.GetBeginAddress());
         p < reinterpret_cast<uintptr_t>(slab.GetEndAddress());
         p += internal::k4KiloBytes) {
      PERFETTO_DCHECK(p % internal::k4KiloBytes == 0);
      block_4KB_aligned_to_slab_.Erase(p);
    }
  }

  SlabType& FindSlabInHashMap(const void* ptr) {
    auto ptr_4KB_aligned = reinterpret_cast<uintptr_t>(ptr) &
                           ~(static_cast<uintptr_t>(internal::k4KiloBytes) - 1);
    SlabType** slab = block_4KB_aligned_to_slab_.Find(ptr_4KB_aligned);
    PERFETTO_CHECK(slab);
    PERFETTO_CHECK(*slab);
    return **slab;
  }

  void DeleteSlabs(SlabList& slabs) {
    while (!slabs.empty()) {
      auto& slab = slabs.front();
      slabs.PopFront();
      delete &slab;
    }
  }

  base::FlatHashMap<uintptr_t, SlabType*> block_4KB_aligned_to_slab_;
  SlabList slabs_full_;
  SlabList slabs_non_full_;
  size_t slabs_non_full_size_{0};
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_SLAB_ALLOCATOR_H_
