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

#ifndef SRC_TRACE_PROCESSOR_CORE_UTIL_SLAB_H_
#define SRC_TRACE_PROCESSOR_CORE_UTIL_SLAB_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core {

// A memory-aligned contiguous block of trivially constructible and destructible
// elements. Basically just a thin wrapper around a std::unique_ptr and a size
// but with enforced alignment and additional compile-time checks.
//
// This class enforces several important constraints:
// - Elements must be trivially constructible and destructible
//
// Usage example:
//   auto slab = Slab<float>::Alloc(1024);  // Allocates space for 1024 floats
//   for (size_t i = 0; i < slab.size(); ++i) {
//     slab[i] = static_cast<float>(i);
//   }
template <typename T>
class Slab {
 public:
  static_assert(std::is_trivially_constructible_v<T>,
                "Slab elements must be trivially constructible");
  static_assert(std::is_trivially_destructible_v<T>,
                "Slab elements must be trivially destructible");

  using value_type = T;
  using const_iterator = const T*;

  // Default constructor creates an empty slab.
  Slab() = default;

  // Move operations are supported.
  constexpr Slab(Slab&&) = default;
  constexpr Slab& operator=(Slab&&) = default;

  // Copy operations are deleted to avoid accidental copies.
  Slab(const Slab&) = delete;
  Slab& operator=(const Slab&) = delete;

  // Alignment of every slab allocation. 64 bytes is the widest SIMD register
  // we target and the alignment Arrow recommends for its buffers, so a slab
  // and a buffer mapped out of an Arrow file are interchangeable.
  static constexpr size_t kAlignment = 64;

  // Allocates a new slab with the specified number of elements.
  //
  // size: Number of elements to allocate space for.
  // Returns a new Slab object with the requested capacity.
  static Slab<T> Alloc(uint64_t size) {
    static_assert(alignof(T) <= kAlignment, "T is over-aligned for a Slab");
    // Allocations too small to hold a whole vector register cannot benefit
    // from the wider alignment, and paying for it on the many tiny slabs the
    // parser creates measurably costs memory.
    uint64_t bytes = size * sizeof(T);
    size_t alignment = bytes >= kAlignment ? kAlignment : alignof(T);
    return Slab(static_cast<T*>(base::AlignedAlloc(alignment, bytes)), size);
  }

  // Shrinks the logical size without changing the underlying allocation.
  void Truncate(uint64_t size) {
    PERFETTO_CHECK(size <= size_);
    size_ = size;
  }

  // Transfers this allocation into a byte slab without copying.
  Slab<uint8_t> TakeAsBytes() && {
    static_assert(std::is_trivially_copyable_v<T>);
    uint64_t bytes = size_ * sizeof(T);
    return Slab<uint8_t>(reinterpret_cast<uint8_t*>(data_.release()), bytes);
  }

  // Returns a pointer to the underlying data.
  PERFETTO_ALWAYS_INLINE const T* data() const { return data_.get(); }
  PERFETTO_ALWAYS_INLINE T* data() { return data_.get(); }

  // Returns the number of elements in the slab.
  PERFETTO_ALWAYS_INLINE uint64_t size() const { return size_; }
  PERFETTO_ALWAYS_INLINE Span<T> mutable_span() {
    return Span<T>(data(), data() + size());
  }
  PERFETTO_ALWAYS_INLINE Span<const T> span() const {
    return Span<const T>(data(), data() + size());
  }
  PERFETTO_ALWAYS_INLINE operator Span<T>() { return mutable_span(); }
  PERFETTO_ALWAYS_INLINE operator Span<const T>() const { return span(); }

  // Returns iterators for range-based for loops.
  PERFETTO_ALWAYS_INLINE T* begin() const { return data_.get(); }
  PERFETTO_ALWAYS_INLINE T* end() const { return data_.get() + size_; }

  // Provides indexed access to elements.
  PERFETTO_ALWAYS_INLINE T& operator[](uint64_t i) const {
    return data_.get()[i];
  }

 private:
  template <typename U>
  friend class Slab;

  // Constructor used by Alloc.
  Slab(T* data, uint64_t size) : data_(data), size_(size) {}

  // Aligned unique pointer that holds the allocated memory.
  base::AlignedUniquePtr<T> data_;

  // Number of elements in the slab.
  uint64_t size_ = 0;
};

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_UTIL_SLAB_H_
