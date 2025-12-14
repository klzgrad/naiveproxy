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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_FLEX_VECTOR_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_FLEX_VECTOR_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/dataframe/impl/slab.h"

namespace perfetto::trace_processor::dataframe::impl {

// A dynamically resizable vector with aligned memory allocation.
//
// FlexVector provides a vector-like container optimized for
// performance-critical operations. It offers significant advantages over
// std::vector:
// 1. Custom memory alignment guarantees for better SIMD performance
// 2. No initialization of elements (avoids constructors for better performance)
// 3. Only works with trivially copyable types for simpler memory management
// 4. Explicit control over memory growth policies
//
// Features:
// - Automatic capacity growth (doubles in capacity when full)
// - Memory alignment for efficient SIMD operations
// - Simple API similar to std::vector but for trivially copyable types only
//
// Performance characteristics:
// - Uses aligned memory for better memory access patterns
// - Provides fast element access with bounds checking in debug mode
//
// Usage example:
//   auto vec = FlexVector<int>::CreateWithCapacity(8);
//   for (int i = 0; i < 20; ++i) {
//     vec.push_back(i);  // Will automatically resize when needed
//   }
template <typename T>
class FlexVector {
 public:
  // The capacity should always be a multiple of this value to ensure
  // proper alignment and memory access patterns.
  static constexpr size_t kCapacityMultiple = 64;

  // The growth factor when the vector runs out of capacity.
  // This is set to 1.5x to avoid excessive memory usage while still
  // providing a reasonable growth rate.
  static constexpr double kGrowthFactor = 1.5;

  static_assert(std::is_trivially_destructible_v<T>,
                "FlexVector elements must be trivially destructible");
  static_assert(std::is_trivially_copyable_v<T>,
                "FlexVector elements must be trivially copyable");

  using value_type = T;
  using size_type = uint64_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;

  // Default constructor creates an empty vector.
  FlexVector() = default;

  // Allocates a new FlexVector with the specified initial capacity.
  //
  // capacity: Initial capacity (number of elements).
  static FlexVector<T> CreateWithCapacity(uint64_t capacity) {
    return FlexVector(base::AlignUp(capacity, kCapacityMultiple), 0);
  }

  // Allocates a new FlexVector with the specified initial size. The values
  // are *not* initialized; this is the main reason why this class exists vs
  // std::vector.
  //
  // size: Initial size (number of elements).
  static FlexVector<T> CreateWithSize(uint64_t size) {
    return FlexVector(base::AlignUp(size, kCapacityMultiple), size);
  }

  // Adds `value` to the end of the vector.
  PERFETTO_ALWAYS_INLINE void push_back(T value) {
    PERFETTO_DCHECK(capacity() % kCapacityMultiple == 0);
    PERFETTO_DCHECK(size_ <= capacity());
    if (PERFETTO_UNLIKELY(size_ == capacity())) {
      IncreaseCapacity();
    }
    slab_[size_++] = value;
  }

  // Adds `count` elements of `value` to the end of the vector.
  PERFETTO_ALWAYS_INLINE void push_back_multiple(T value, uint64_t count) {
    PERFETTO_DCHECK(capacity() % kCapacityMultiple == 0);
    PERFETTO_DCHECK(size_ <= capacity());
    while (PERFETTO_UNLIKELY(size_ + count > capacity())) {
      IncreaseCapacity();
    }
    uint64_t end = size_ + count;
    for (; size_ < end; ++size_) {
      slab_[size_] = value;
    }
  }

  // Removes the last element from the vector. Should not be called on an
  // empty vector.
  PERFETTO_ALWAYS_INLINE void pop_back() {
    PERFETTO_DCHECK(size_ > 0);
    --size_;
  }

  // Provides indexed access to elements with bounds checking in debug mode.
  PERFETTO_ALWAYS_INLINE T& operator[](uint64_t i) {
    PERFETTO_DCHECK(i < size_);
    return slab_.data()[i];
  }

  PERFETTO_ALWAYS_INLINE const T& operator[](uint64_t i) const {
    PERFETTO_DCHECK(i < size_);
    return slab_.data()[i];
  }

  // Clears the vector, resetting its size to zero.
  void clear() { size_ = 0; }

  // Shrinks the memory allocated by the vector to be as small as possible while
  // still maintaining the invariants of the class.
  void shrink_to_fit() {
    if (size_ == 0) {
      slab_ = Slab<T>::Alloc(0);
    } else {
      Slab<T> new_slab =
          Slab<T>::Alloc(base::AlignUp(size_, kCapacityMultiple));
      memcpy(new_slab.data(), slab_.data(), size_ * sizeof(T));
      slab_ = std::move(new_slab);
    }
  }

  // Access to the underlying data and size.
  PERFETTO_ALWAYS_INLINE T* data() { return slab_.data(); }
  PERFETTO_ALWAYS_INLINE const T* data() const { return slab_.data(); }
  PERFETTO_ALWAYS_INLINE uint64_t size() const { return size_; }
  PERFETTO_ALWAYS_INLINE bool empty() const { return size() == 0; }

  // Iterators for range-based for loops.
  PERFETTO_ALWAYS_INLINE const T* begin() const { return slab_.data(); }
  PERFETTO_ALWAYS_INLINE const T* end() const { return slab_.data() + size_; }
  PERFETTO_ALWAYS_INLINE T* begin() { return slab_.data(); }
  PERFETTO_ALWAYS_INLINE T* end() { return slab_.data() + size_; }

  PERFETTO_ALWAYS_INLINE const T& back() const {
    PERFETTO_DCHECK(!empty());
    return slab_.data()[size_ - 1];
  }
  PERFETTO_ALWAYS_INLINE T& back() {
    PERFETTO_DCHECK(!empty());
    return slab_.data()[size_ - 1];
  }

  // Returns the current capacity (maximum size without reallocation).
  PERFETTO_ALWAYS_INLINE uint64_t capacity() const { return slab_.size(); }

 private:
  // Constructor used by Alloc.
  explicit FlexVector(uint64_t capacity, uint64_t size)
      : slab_(Slab<T>::Alloc(capacity)), size_(size) {}

  PERFETTO_NO_INLINE void IncreaseCapacity() {
    uint64_t new_capacity = std::max<uint64_t>(
        base::AlignUp(static_cast<size_t>(static_cast<double>(capacity()) *
                                          kGrowthFactor),
                      kCapacityMultiple),
        kCapacityMultiple);
    Slab<T> new_slab = Slab<T>::Alloc(new_capacity);
    if (slab_.size() > 0) {
      // Copy from the original slab data
      memcpy(new_slab.data(), slab_.data(), size_ * sizeof(T));
    }
    slab_ = std::move(new_slab);
  }

  // The underlying memory slab.
  Slab<T> slab_;

  // Current number of elements.
  uint64_t size_ = 0;
};

}  // namespace perfetto::trace_processor::dataframe::impl

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_FLEX_VECTOR_H_
