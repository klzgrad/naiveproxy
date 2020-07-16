// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// unique_ptr-style pointer that stores values that may be from an arena. Takes
// up the same storage as the platform's native pointer type. Takes ownership
// of the value it's constructed with; if holding a value in an arena, and the
// type has a non-trivial destructor, the arena must outlive the
// QuicArenaScopedPtr. Does not support array overloads.

#ifndef QUICHE_QUIC_CORE_QUIC_ARENA_SCOPED_PTR_H_
#define QUICHE_QUIC_CORE_QUIC_ARENA_SCOPED_PTR_H_

#include <cstdint>  // for uintptr_t

#include "net/third_party/quiche/src/quic/platform/api/quic_aligned.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

template <typename T>
class QUIC_NO_EXPORT QuicArenaScopedPtr {
  static_assert(QUIC_ALIGN_OF(T*) > 1,
                "QuicArenaScopedPtr can only store objects that are aligned to "
                "greater than 1 byte.");

 public:
  // Constructs an empty QuicArenaScopedPtr.
  QuicArenaScopedPtr();

  // Constructs a QuicArenaScopedPtr referencing the heap-allocated memory
  // provided.
  explicit QuicArenaScopedPtr(T* value);

  template <typename U>
  QuicArenaScopedPtr(QuicArenaScopedPtr<U>&& other);  // NOLINT
  template <typename U>
  QuicArenaScopedPtr& operator=(QuicArenaScopedPtr<U>&& other);
  ~QuicArenaScopedPtr();

  // Returns a pointer to the value.
  T* get() const;

  // Returns a reference to the value.
  T& operator*() const;

  // Returns a pointer to the value.
  T* operator->() const;

  // Swaps the value of this pointer with |other|.
  void swap(QuicArenaScopedPtr& other);

  // Resets the held value to |value|.
  void reset(T* value = nullptr);

  // Returns true if |this| came from an arena. Primarily exposed for testing
  // and assertions.
  bool is_from_arena();

 private:
  // Friends with other derived types of QuicArenaScopedPtr, to support the
  // derived-types case.
  template <typename U>
  friend class QuicArenaScopedPtr;
  // Also befriend all known arenas, only to prevent misuse.
  template <uint32_t ArenaSize>
  friend class QuicOneBlockArena;

  // Tag to denote that a QuicArenaScopedPtr is being explicitly created by an
  // arena.
  enum class ConstructFrom { kHeap, kArena };

  // Constructs a QuicArenaScopedPtr with the given representation.
  QuicArenaScopedPtr(void* value, ConstructFrom from);
  QuicArenaScopedPtr(const QuicArenaScopedPtr&) = delete;
  QuicArenaScopedPtr& operator=(const QuicArenaScopedPtr&) = delete;

  // Low-order bits of value_ that determine if the pointer came from an arena.
  static const uintptr_t kFromArenaMask = 0x1;

  // Every platform we care about has at least 4B aligned integers, so store the
  // is_from_arena bit in the least significant bit.
  void* value_;
};

template <typename T>
bool operator==(const QuicArenaScopedPtr<T>& left,
                const QuicArenaScopedPtr<T>& right) {
  return left.get() == right.get();
}

template <typename T>
bool operator!=(const QuicArenaScopedPtr<T>& left,
                const QuicArenaScopedPtr<T>& right) {
  return left.get() != right.get();
}

template <typename T>
bool operator==(std::nullptr_t, const QuicArenaScopedPtr<T>& right) {
  return nullptr == right.get();
}

template <typename T>
bool operator!=(std::nullptr_t, const QuicArenaScopedPtr<T>& right) {
  return nullptr != right.get();
}

template <typename T>
bool operator==(const QuicArenaScopedPtr<T>& left, std::nullptr_t) {
  return left.get() == nullptr;
}

template <typename T>
bool operator!=(const QuicArenaScopedPtr<T>& left, std::nullptr_t) {
  return left.get() != nullptr;
}

template <typename T>
QuicArenaScopedPtr<T>::QuicArenaScopedPtr() : value_(nullptr) {}

template <typename T>
QuicArenaScopedPtr<T>::QuicArenaScopedPtr(T* value)
    : QuicArenaScopedPtr(value, ConstructFrom::kHeap) {}

template <typename T>
template <typename U>
QuicArenaScopedPtr<T>::QuicArenaScopedPtr(QuicArenaScopedPtr<U>&& other)
    : value_(other.value_) {
  static_assert(
      std::is_base_of<T, U>::value || std::is_same<T, U>::value,
      "Cannot construct QuicArenaScopedPtr; type is not derived or same.");
  other.value_ = nullptr;
}

template <typename T>
template <typename U>
QuicArenaScopedPtr<T>& QuicArenaScopedPtr<T>::operator=(
    QuicArenaScopedPtr<U>&& other) {
  static_assert(
      std::is_base_of<T, U>::value || std::is_same<T, U>::value,
      "Cannot assign QuicArenaScopedPtr; type is not derived or same.");
  swap(other);
  return *this;
}

template <typename T>
QuicArenaScopedPtr<T>::~QuicArenaScopedPtr() {
  reset();
}

template <typename T>
T* QuicArenaScopedPtr<T>::get() const {
  return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(value_) &
                              ~kFromArenaMask);
}

template <typename T>
T& QuicArenaScopedPtr<T>::operator*() const {
  return *get();
}

template <typename T>
T* QuicArenaScopedPtr<T>::operator->() const {
  return get();
}

template <typename T>
void QuicArenaScopedPtr<T>::swap(QuicArenaScopedPtr& other) {
  using std::swap;
  swap(value_, other.value_);
}

template <typename T>
bool QuicArenaScopedPtr<T>::is_from_arena() {
  return (reinterpret_cast<uintptr_t>(value_) & kFromArenaMask) != 0;
}

template <typename T>
void QuicArenaScopedPtr<T>::reset(T* value) {
  if (value_ != nullptr) {
    if (is_from_arena()) {
      // Manually invoke the destructor.
      get()->~T();
    } else {
      delete get();
    }
  }
  DCHECK_EQ(0u, reinterpret_cast<uintptr_t>(value) & kFromArenaMask);
  value_ = value;
}

template <typename T>
QuicArenaScopedPtr<T>::QuicArenaScopedPtr(void* value, ConstructFrom from_arena)
    : value_(value) {
  DCHECK_EQ(0u, reinterpret_cast<uintptr_t>(value_) & kFromArenaMask);
  switch (from_arena) {
    case ConstructFrom::kHeap:
      break;
    case ConstructFrom::kArena:
      value_ = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(value_) |
                                       QuicArenaScopedPtr<T>::kFromArenaMask);
      break;
  }
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_ARENA_SCOPED_PTR_H_
