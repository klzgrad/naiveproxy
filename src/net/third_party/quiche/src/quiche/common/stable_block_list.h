// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_STABLE_BLOCK_LIST_H_
#define QUICHE_COMMON_STABLE_BLOCK_LIST_H_

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {

// StableBlockList: A block-based append-only sequence container with iterator
// stability.
//
// StableBlockList is designed for use cases where elements are only appended to
// the end or erased from arbitrary positions, such as the backing store of a
// LinkedHashMap.
//
// It provides 100% iterator stability on push_back/emplace_back and erase.
// Iterators to erased elements are invalidated, but all other iterators remain
// valid.
//
// Internally, it manages a doubly-linked list of fixed-capacity blocks.
template <typename T, size_t BlockCapacity = 16,   // QUICHE_NO_EXPORT
          typename Allocator = std::allocator<T>>  // QUICHE_NO_EXPORT
class StableBlockList {                            // QUICHE_NO_EXPORT
  static_assert(BlockCapacity > 0, "BlockCapacity must be greater than 0");
  static_assert(BlockCapacity <= 64, "BlockCapacity cannot exceed 64");

 private:
  // Selects the smallest unsigned integer type that can hold BlockCapacity
  // bits. This minimizes block metadata overhead (2B for capacity<=16, 4B for
  // capacity<=32, 8B for capacity<=64).
  using OccupancyType = std::conditional_t<
      BlockCapacity <= 16, uint16_t,
      std::conditional_t<BlockCapacity <= 32, uint32_t, uint64_t>>;

  static constexpr int kBitWidth = sizeof(OccupancyType) * 8;

  // --- Bitwise Math & Manipulation Helpers ---
  // These map to single-cycle CPU instructions (e.g. tzcnt, lzcnt, bsf, bsr)
  // for O(1) operations.

  // Finds the index of the lowest set bit. Returns default_idx if none.
  static constexpr int LowestBitIndex(OccupancyType bits, int default_idx) {
    return bits != 0 ? std::countr_zero(bits) : default_idx;
  }

  // Finds the index of the highest set bit. Returns default_idx if none.
  static constexpr int HighestBitIndex(OccupancyType bits, int default_idx) {
    return bits != 0 ? (kBitWidth - 1 - std::countl_zero(bits)) : default_idx;
  }

  // Clears (zeros out) all bits at indices <= start_idx.
  [[nodiscard]] static constexpr OccupancyType ClearLowBits(OccupancyType bits,
                                                            int start_idx) {
    QUICHE_DCHECK_GE(start_idx, 0);
    QUICHE_DCHECK_LE(start_idx, kBitWidth);
    if constexpr (kBitWidth < 64) {
      // Safe to shift without guard because start_idx <= kBitWidth <= 32.
      uint64_t mask = ~((uint64_t{1} << start_idx) - 1);
      return static_cast<OccupancyType>(static_cast<uint64_t>(bits) & mask);
    } else {
      if (start_idx >= 64) return 0;
      uint64_t mask = ~((uint64_t{1} << start_idx) - 1);
      return static_cast<OccupancyType>(bits & mask);
    }
  }

  // Clears (zeros out) all bits at indices >= end_idx.
  [[nodiscard]] static constexpr OccupancyType ClearHighBits(OccupancyType bits,
                                                             int end_idx) {
    QUICHE_DCHECK_GE(end_idx, 0);
    QUICHE_DCHECK_LE(end_idx, kBitWidth);
    if (end_idx <= 0) return 0;
    if constexpr (kBitWidth < 64) {
      // Safe to shift without guard because end_idx <= kBitWidth <= 32.
      uint64_t mask = (uint64_t{1} << end_idx) - 1;
      return static_cast<OccupancyType>(static_cast<uint64_t>(bits) & mask);
    } else {
      if (end_idx >= 64) return bits;
      uint64_t mask = (uint64_t{1} << end_idx) - 1;
      return static_cast<OccupancyType>(bits & mask);
    }
  }

  static constexpr bool IsBitSet(OccupancyType bits, int index) {
    return (bits & (OccupancyType{1} << index)) != 0;
  }

  [[nodiscard]] static constexpr OccupancyType SetBit(OccupancyType bits,
                                                      int index) {
    return bits | (OccupancyType{1} << index);
  }

  [[nodiscard]] static constexpr OccupancyType ClearBit(OccupancyType bits,
                                                        int index) {
    return bits & ~(OccupancyType{1} << index);
  }

  struct Block;

  // Lightweight header for blocks to allow sentinel node in ControlBlock
  // without wasting memory for elements.
  struct BlockHeader {
    BlockHeader* prev = this;  // Not used when a block is in the freelist.
    BlockHeader* next = this;
    OccupancyType occupancy = 0;
  };

  // Fixed-capacity node (block) holding contiguous elements.
  struct Block : public BlockHeader {
    alignas(T) char data[BlockCapacity *
                         sizeof(T)];  // Uninitialized storage for elements

    T* ElementAt(size_t index) { return reinterpret_cast<T*>(data) + index; }
    const T* ElementAt(size_t index) const {
      return reinterpret_cast<const T*>(data) + index;
    }
  };

  using BlockAllocator =
      typename std::allocator_traits<Allocator>::template rebind_alloc<Block>;

  // ControlBlock holds metadata and sentinel. It is heap-allocated and
  // owned via unique_ptr to keep block pointers and iterators stable when the
  // list is moved.
  struct ControlBlock {
    BlockHeader sentinel;          // next/prev point to the head/tail
    size_t size = 0;               // Total number of elements in the list
    Block* free_blocks = nullptr;  // Singly-linked LIFO pool of recycled blocks
  };

 public:
  template <bool is_const>
  class IteratorImpl {
   public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = typename std::conditional<is_const, const T*, T*>::type;
    using reference = typename std::conditional<is_const, const T&, T&>::type;

    IteratorImpl() = default;

    // Iterators reference elements by their block pointer and offset.
    IteratorImpl(BlockHeader* block, uint32_t block_index)
        : block_(block), block_index_(block_index) {}

    // Allows implicit conversion from iterator to const_iterator.
    template <bool was_const = is_const,
              typename = typename std::enable_if<was_const>::type>
    IteratorImpl(const IteratorImpl<false>& other)
        : block_(other.block_), block_index_(other.block_index_) {}

    reference operator*() const {
      ABSL_HARDENING_ASSERT(block_ != nullptr);
      ABSL_HARDENING_ASSERT(block_->occupancy != 0 &&
                            "Attempted to dereference end()");
      return *static_cast<pointer_to_block>(block_)->ElementAt(block_index_);
    }
    pointer operator->() const {
      ABSL_HARDENING_ASSERT(block_ != nullptr);
      ABSL_HARDENING_ASSERT(block_->occupancy != 0 &&
                            "Attempted to dereference end()");
      return static_cast<pointer_to_block>(block_)->ElementAt(block_index_);
    }

    IteratorImpl& operator++() {
      *this = Next(*this);
      return *this;
    }

    IteratorImpl operator++(int) {
      IteratorImpl tmp = *this;
      ++*this;
      return tmp;
    }

    IteratorImpl& operator--() {
      *this = Prev(*this);
      return *this;
    }

    IteratorImpl operator--(int) {
      IteratorImpl tmp = *this;
      --*this;
      return tmp;
    }

    bool operator==(const IteratorImpl& other) const = default;

   private:
    using pointer_to_block =
        typename std::conditional<is_const, const Block*, Block*>::type;

    BlockHeader* block_ = nullptr;
    uint32_t block_index_ = 0;

    friend class StableBlockList;
    template <bool>
    friend class IteratorImpl;
  };

  using value_type = T;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = IteratorImpl<false>;
  using const_iterator = IteratorImpl<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  // Copies are not allowed.
  StableBlockList(const StableBlockList&) = delete;
  StableBlockList& operator=(const StableBlockList&) = delete;

  // Moves are allowed.
  StableBlockList(StableBlockList&& other) noexcept
      : allocator_(std::move(other.allocator_)),
        control_block_(std::move(other.control_block_)) {}

  StableBlockList& operator=(StableBlockList&& other) noexcept {
    if (this != &other) {
      this->~StableBlockList();
      new (this) StableBlockList(std::move(other));
    }
    return *this;
  }

  explicit StableBlockList(const Allocator& alloc = Allocator())
      : allocator_(alloc), control_block_(std::make_unique<ControlBlock>()) {}

  ~StableBlockList() {
    if (!control_block_) return;
    clear();
    shrink_to_fit();
  }

  bool empty() const {
    return control_block_ ? (control_block_->size == 0) : true;
  }
  size_t size() const { return control_block_ ? control_block_->size : 0; }

  // Standard element accessors.
  T& front() {
    return const_cast<T&>(const_cast<const StableBlockList*>(this)->front());
  }
  const T& front() const {
    QUICHE_DCHECK(!empty());
    return *begin();
  }

  T& back() {
    return const_cast<T&>(const_cast<const StableBlockList*>(this)->back());
  }
  const T& back() const {
    QUICHE_DCHECK(!empty());
    return *rbegin();
  }

  // Standard iterator accessors.
  iterator begin() {
    if (empty()) {
      return end();
    }
    BlockHeader* head = control_block_->sentinel.next;
    return iterator(head, LowestBitIndex(head->occupancy, BlockCapacity));
  }
  const_iterator begin() const {
    return const_cast<StableBlockList*>(this)->begin();
  }
  const_iterator cbegin() const { return begin(); }

  iterator end() {
    return iterator(control_block_ ? &control_block_->sentinel : nullptr, 0);
  }
  const_iterator end() const {
    return const_iterator(
        control_block_ ? const_cast<BlockHeader*>(&control_block_->sentinel)
                       : nullptr,
        0);
  }
  const_iterator cend() const { return end(); }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator crbegin() const {
    return const_reverse_iterator(end());
  }

  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }
  const_reverse_iterator crend() const {
    return const_reverse_iterator(begin());
  }

  // Elements may be appended.
  void push_back(const T& value) { InsertToEnd(value); }
  void push_back(T&& value) { InsertToEnd(std::move(value)); }

  // TODO(birenroy): Remove these restricted insert/emplace methods once
  // QuicheLinkedHashMap is updated to use push_back/emplace_back.
  iterator insert(const_iterator pos, const T& value) {
    QUICHE_DCHECK(pos == end())
        << "StableBlockList only supports insertion at the end";
    return InsertToEnd(value);
  }
  iterator insert(const_iterator pos, T&& value) {
    QUICHE_DCHECK(pos == end())
        << "StableBlockList only supports insertion at the end";
    return InsertToEnd(std::move(value));
  }

  template <typename... Args>
  iterator emplace(const_iterator pos, Args&&... args) {
    QUICHE_DCHECK(pos == end())
        << "StableBlockList only supports emplace at the end";
    return InsertToEnd(std::forward<Args>(args)...);
  }

  // Swaps are efficient, and do not require any copies.
  void swap(StableBlockList& other) noexcept {
    using std::swap;
    swap(control_block_, other.control_block_);
    if constexpr (std::allocator_traits<
                      Allocator>::propagate_on_container_swap::value) {
      swap(allocator_, other.allocator_);
    }
  }

  // Elements may be erased at any position.
  iterator erase(const_iterator pos) {
    BlockHeader* header = pos.block_;
    QUICHE_DCHECK(header != &control_block_->sentinel);
    Block* block = static_cast<Block*>(header);

    // Capture the next element before destroying the current one
    iterator next_it = Next(iterator(header, pos.block_index_));

    // Destroy the element and clear its occupancy bit
    std::allocator_traits<Allocator>::destroy(
        allocator_, block->ElementAt(pos.block_index_));
    block->occupancy = ClearBit(block->occupancy, pos.block_index_);
    --control_block_->size;

    // If the block becomes completely empty, unlink and recycle it
    if (block->occupancy == 0) {
      BlockHeader* prev = block->prev;
      BlockHeader* next = block->next;

      prev->next = next;
      next->prev = prev;

      DeallocateBlock(block);
    }

    // If we erased the last element, next_it will be end() which points to
    // sentinel.
    return next_it;
  }

  iterator erase(const_iterator first, const_iterator last) {
    while (first != last && first != end()) {
      first = erase(first);
    }
    return iterator(first.block_, first.block_index_);
  }

  // The clear operation removes all elements from the list, but does not free
  // the memory of any allocated blocks. These are appended to the freelist.
  void clear() {
    if (!control_block_) return;
    BlockHeader* curr = control_block_->sentinel.next;
    while (curr != &control_block_->sentinel) {
      Block* block = static_cast<Block*>(curr);
      BlockHeader* next = curr->next;
      for (size_t i = 0; i < BlockCapacity; ++i) {
        if (IsBitSet(block->occupancy, i)) {
          std::allocator_traits<Allocator>::destroy(allocator_,
                                                    block->ElementAt(i));
        }
      }
      DeallocateBlock(block);
      curr = next;
    }
    control_block_->sentinel.next = &control_block_->sentinel;
    control_block_->sentinel.prev = &control_block_->sentinel;
    control_block_->size = 0;
    // Note: we keep control_block_->free_blocks for reuse.
  }

  // Releases all recycled blocks in the free list to free memory.
  //
  // Note: This method does not compact elements in active blocks or relocate
  // them, as doing so would invalidate iterators and references, violating
  // the iterator stability guarantee of StableBlockList. It only releases
  // completely empty blocks that have been cached for reuse.
  void shrink_to_fit() {
    if (!control_block_) return;
    BlockAllocator block_alloc(allocator_);
    Block* curr = control_block_->free_blocks;
    while (curr != nullptr) {
      Block* next = static_cast<Block*>(curr->next);
      std::allocator_traits<BlockAllocator>::deallocate(block_alloc, curr, 1);
      curr = next;
    }
    control_block_->free_blocks = nullptr;
  }

 private:
  Block* AllocateBlock() {
    BlockAllocator block_alloc(allocator_);
    Block* block = nullptr;
    if (control_block_->free_blocks != nullptr) {
      block = control_block_->free_blocks;
      control_block_->free_blocks = static_cast<Block*>(block->next);
    } else {
      block = std::allocator_traits<BlockAllocator>::allocate(block_alloc, 1);
    }
    std::allocator_traits<BlockAllocator>::construct(block_alloc, block);
    return block;
  }

  void DeallocateBlock(Block* block) {
    // Link to free blocks LIFO list using 'next' pointer
    block->next = control_block_->free_blocks;
    control_block_->free_blocks = block;
  }

  template <typename It>
  static It Next(It it) {
    BlockHeader* curr = it.block_;
    // Handles default-constructed iterators and end() of moved-from lists.
    if (curr == nullptr) {
      return it;
    }
    uint32_t i = it.block_index_;

    // Incrementing end() is undefined behavior.
    QUICHE_DCHECK(curr->occupancy != 0)
        << "Attempted to increment end() iterator";

    // Find the next set bit in the current block's occupancy mask after index
    // 'i'
    OccupancyType bits = ClearLowBits(curr->occupancy, i + 1);
    int next_idx = LowestBitIndex(bits, BlockCapacity);
    if (next_idx < static_cast<int>(BlockCapacity)) {
      return It(curr, next_idx);
    }

    // If no more elements in this block, jump to the next block.
    // Invariant: empty blocks are immediately unlinked in erase(), so any
    // linked next block (if it's not the sentinel) is guaranteed to have
    // occupancy > 0.
    BlockHeader* next_b = curr->next;
    if (next_b->occupancy != 0) {
      int first_idx = LowestBitIndex(next_b->occupancy, BlockCapacity);
      QUICHE_DCHECK_LT(first_idx, static_cast<int>(BlockCapacity));
      return It(next_b, first_idx);
    }

    // Reached sentinel (end())
    return It(next_b, 0);
  }

  template <typename It>
  static It Prev(It it) {
    BlockHeader* curr = it.block_;
    if (curr == nullptr) {
      return it;
    }
    uint32_t i = it.block_index_;

    // Find the previous set bit in the current block's occupancy mask before
    // index 'i'
    OccupancyType bits = ClearHighBits(curr->occupancy, i);
    int prev_idx = HighestBitIndex(bits, -1);
    if (prev_idx >= 0) {
      return It(curr, prev_idx);
    }

    // If no previous elements in this block, jump to the previous block.
    BlockHeader* prev_b = curr->prev;
    if (prev_b->occupancy != 0) {  // If not sentinel
      int last_idx = HighestBitIndex(prev_b->occupancy, -1);
      QUICHE_DCHECK_GE(last_idx, 0);
      return It(prev_b, last_idx);
    }

    // Reached sentinel (begin() decrement goes to end())
    return It(prev_b, 0);
  }

  template <typename... Args>
  iterator InsertToEnd(Args&&... args) {
    EnsureControlBlock();

    BlockHeader* sentinel = &control_block_->sentinel;
    BlockHeader* tail_header = sentinel->prev;
    Block* tail = static_cast<Block*>(tail_header);

    int insert_idx = 0;
    bool need_new_block = false;

    if (tail_header == sentinel) {
      need_new_block = true;
    } else {
      int k = HighestBitIndex(tail->occupancy, -1);
      if (k < static_cast<int>(BlockCapacity) - 1) {
        insert_idx = k + 1;
      } else {
        need_new_block = true;
      }
    }

    if (need_new_block) {
      Block* new_block = AllocateBlock();
      new_block->prev = tail_header;
      new_block->next = sentinel;
      tail_header->next = new_block;
      sentinel->prev = new_block;
      tail = new_block;
      insert_idx = 0;
    }

    std::allocator_traits<Allocator>::construct(
        allocator_, tail->ElementAt(insert_idx), std::forward<Args>(args)...);
    tail->occupancy = SetBit(tail->occupancy, insert_idx);
    control_block_->size++;
    return iterator(tail, insert_idx);
  }

  void EnsureControlBlock() {
    if (!control_block_) {
      control_block_ = std::make_unique<ControlBlock>();
    }
  }

  ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS Allocator allocator_;
  absl_nullable std::unique_ptr<ControlBlock> control_block_;
};

template <typename T, size_t BlockCapacity, typename Allocator>
void swap(StableBlockList<T, BlockCapacity, Allocator>& lhs,
          StableBlockList<T, BlockCapacity, Allocator>& rhs) noexcept {
  lhs.swap(rhs);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_STABLE_BLOCK_LIST_H_
