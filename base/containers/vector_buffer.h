// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CONTAINERS_VECTOR_BUFFERS_H_
#define BASE_CONTAINERS_VECTOR_BUFFERS_H_

#include <stdlib.h>
#include <string.h>

#include <type_traits>
#include <utility>

#include "base/logging.h"
#include "base/macros.h"

namespace base {
namespace internal {

// Internal implementation detail of base/containers.
//
// Implements a vector-like buffer that holds a certain capacity of T. Unlike
// std::vector, VectorBuffer never constructs or destructs its arguments, and
// can't change sizes. But it does implement templates to assist in efficient
// moving and destruction of those items manually.
//
// In particular, the destructor function does not iterate over the items if
// there is no destructor. Moves should be implemented as a memcpy/memmove for
// trivially copyable objects (POD) otherwise, it should be a std::move if
// possible, and as a last resort it falls back to a copy. This behavior is
// similar to std::vector.
//
// No special consideration is done for noexcept move constructors since
// we compile without exceptions.
//
// The current API does not support moving overlapping ranges.
template <typename T>
class VectorBuffer {
 public:
  constexpr VectorBuffer() = default;

#if defined(__clang__) && !defined(__native_client__)
  // This constructor converts an uninitialized void* to a T* which triggers
  // clang Control Flow Integrity. Since this is as-designed, disable.
  __attribute__((no_sanitize("cfi-unrelated-cast", "vptr")))
#endif
  VectorBuffer(size_t count)
      : buffer_(reinterpret_cast<T*>(malloc(sizeof(T) * count))),
        capacity_(count) {
  }
  VectorBuffer(VectorBuffer&& other) noexcept
      : buffer_(other.buffer_), capacity_(other.capacity_) {
    other.buffer_ = nullptr;
    other.capacity_ = 0;
  }

  ~VectorBuffer() { free(buffer_); }

  VectorBuffer& operator=(VectorBuffer&& other) {
    free(buffer_);
    buffer_ = other.buffer_;
    capacity_ = other.capacity_;

    other.buffer_ = nullptr;
    other.capacity_ = 0;
    return *this;
  }

  size_t capacity() const { return capacity_; }

  T& operator[](size_t i) { return buffer_[i]; }
  const T& operator[](size_t i) const { return buffer_[i]; }
  T* begin() { return buffer_; }
  T* end() { return &buffer_[capacity_]; }

  // DestructRange ------------------------------------------------------------

  // Trivially destructible objects need not have their destructors called.
  template <typename T2 = T,
            typename std::enable_if<std::is_trivially_destructible<T2>::value,
                                    int>::type = 0>
  void DestructRange(T* begin, T* end) {}

  // Non-trivially destructible objects must have their destructors called
  // individually.
  template <typename T2 = T,
            typename std::enable_if<!std::is_trivially_destructible<T2>::value,
                                    int>::type = 0>
  void DestructRange(T* begin, T* end) {
    while (begin != end) {
      begin->~T();
      begin++;
    }
  }

  // MoveRange ----------------------------------------------------------------
  //
  // The destructor will be called (as necessary) for all moved types. The
  // ranges must not overlap.
  //
  // The parameters and begin and end (one past the last) of the input buffer,
  // and the address of the first element to copy to. There must be sufficient
  // room in the destination for all items in the range [begin, end).

  // Trivially copyable types can use memcpy. trivially copyable implies
  // that there is a trivial destructor as we don't have to call it.
  template <typename T2 = T,
            typename std::enable_if<base::is_trivially_copyable<T2>::value,
                                    int>::type = 0>
  static void MoveRange(T* from_begin, T* from_end, T* to) {
    DCHECK(!RangesOverlap(from_begin, from_end, to));
    memcpy(to, from_begin, (from_end - from_begin) * sizeof(T));
  }

  // Not trivially copyable, but movable: call the move constructor and
  // destruct the original.
  template <typename T2 = T,
            typename std::enable_if<std::is_move_constructible<T2>::value &&
                                        !base::is_trivially_copyable<T2>::value,
                                    int>::type = 0>
  static void MoveRange(T* from_begin, T* from_end, T* to) {
    DCHECK(!RangesOverlap(from_begin, from_end, to));
    while (from_begin != from_end) {
      new (to) T(std::move(*from_begin));
      from_begin->~T();
      from_begin++;
      to++;
    }
  }

  // Not movable, not trivially copyable: call the copy constructor and
  // destruct the original.
  template <typename T2 = T,
            typename std::enable_if<!std::is_move_constructible<T2>::value &&
                                        !base::is_trivially_copyable<T2>::value,
                                    int>::type = 0>
  static void MoveRange(T* from_begin, T* from_end, T* to) {
    DCHECK(!RangesOverlap(from_begin, from_end, to));
    while (from_begin != from_end) {
      new (to) T(*from_begin);
      from_begin->~T();
      from_begin++;
      to++;
    }
  }

 private:
  static bool RangesOverlap(const T* from_begin,
                            const T* from_end,
                            const T* to) {
    return !(to >= from_end || to + (from_end - from_begin) <= from_begin);
  }

  T* buffer_ = nullptr;
  size_t capacity_ = 0;

  DISALLOW_COPY_AND_ASSIGN(VectorBuffer);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_CONTAINERS_VECTOR_BUFFERS_H_
