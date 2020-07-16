// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_CHECKED_PTR_H_
#define BASE_MEMORY_CHECKED_PTR_H_

#include <cstddef>
#include <cstdint>
#include <utility>

#include "base/compiler_specific.h"

namespace base {

// NOTE: All methods should be ALWAYS_INLINE. CheckedPtr is meant to be a
// lightweight replacement of a raw pointer, hence performance is critical.

namespace internal {
// These classes/structures are part of the CheckedPtr implementation.
// DO NOT USE THESE CLASSES DIRECTLY YOURSELF.

struct CheckedPtrNoOpImpl {
  // Wraps a pointer, and returns its uintptr_t representation.
  static ALWAYS_INLINE uintptr_t WrapRawPtr(const void* const_ptr) {
    return reinterpret_cast<uintptr_t>(const_ptr);
  }

  // Returns equivalent of |WrapRawPtr(nullptr)|. Separated out to make it a
  // constexpr.
  static constexpr ALWAYS_INLINE uintptr_t GetWrappedNullPtr() {
    // This relies on nullptr and 0 being equal in the eyes of reinterpret_cast,
    // which apparently isn't true in all environments.
    return 0;
  }

  // Unwraps the pointer's uintptr_t representation, while asserting that memory
  // hasn't been freed. The function is allowed to crash on nullptr.
  static ALWAYS_INLINE void* SafelyUnwrapPtrForDereference(
      uintptr_t wrapped_ptr) {
    return reinterpret_cast<void*>(wrapped_ptr);
  }

  // Unwraps the pointer's uintptr_t representation, while asserting that memory
  // hasn't been freed. The function must handle nullptr gracefully.
  static ALWAYS_INLINE void* SafelyUnwrapPtrForExtraction(
      uintptr_t wrapped_ptr) {
    return reinterpret_cast<void*>(wrapped_ptr);
  }

  // Unwraps the pointer's uintptr_t representation, without making an assertion
  // on whether memory was freed or not.
  static ALWAYS_INLINE void* UnsafelyUnwrapPtrForComparison(
      uintptr_t wrapped_ptr) {
    return reinterpret_cast<void*>(wrapped_ptr);
  }

  // Advance the wrapped pointer by |delta| bytes.
  static ALWAYS_INLINE uintptr_t Advance(uintptr_t wrapped_ptr, size_t delta) {
    return wrapped_ptr + delta;
  }

  // This is for accounting only, used by unit tests.
  static ALWAYS_INLINE void IncrementSwapCountForTest() {}
};

template <typename T>
struct DereferencedPointerType {
  using Type = decltype(*std::declval<T*>());
};
// This explicitly doesn't define any type aliases, since dereferencing void is
// invalid.
template <>
struct DereferencedPointerType<void> {};

}  // namespace internal

// DO NOT USE! EXPERIMENTAL ONLY! This is helpful for local testing!
//
// CheckedPtr is meant to be a pointer wrapper, that will crash on
// Use-After-Free (UaF) to prevent security issues. This is very much in the
// experimental phase. More context in:
// https://docs.google.com/document/d/1pnnOAIz_DMWDI4oIOFoMAqLnf_MZ2GsrJNb_dbQ3ZBg
//
// For now, CheckedPtr is a no-op wrapper to aid local testing.
//
// Goals for this API:
// 1. Minimize amount of caller-side changes as much as physically possible.
// 2. Keep this class as small as possible, while still satisfying goal #1 (i.e.
//    we aren't striving to maximize compatibility with raw pointers, merely
//    adding support for cases encountered so far).
template <typename T, typename Impl = internal::CheckedPtrNoOpImpl>
class CheckedPtr {
 public:
  // CheckedPtr can be trivially default constructed (leaving |wrapped_ptr_|
  // uninitialized).  This is needed for compatibility with raw pointers.
  //
  // TODO(lukasza): Always initialize |wrapped_ptr_|.  Fix resulting build
  // errors.  Analyze performance impact.
  constexpr CheckedPtr() noexcept = default;

  // Deliberately implicit, because CheckedPtr is supposed to resemble raw ptr.
  // NOLINTNEXTLINE(runtime/explicit)
  constexpr ALWAYS_INLINE CheckedPtr(std::nullptr_t) noexcept
      : wrapped_ptr_(Impl::GetWrappedNullPtr()) {}

  // Deliberately implicit, because CheckedPtr is supposed to resemble raw ptr.
  // NOLINTNEXTLINE(runtime/explicit)
  ALWAYS_INLINE CheckedPtr(T* p) noexcept : wrapped_ptr_(Impl::WrapRawPtr(p)) {}

  // In addition to nullptr_t ctor above, CheckedPtr needs to have these
  // as |=default| or |constexpr| to avoid hitting -Wglobal-constructors in
  // cases like this:
  //     struct SomeStruct { int int_field; CheckedPtr<int> ptr_field; };
  //     SomeStruct g_global_var = { 123, nullptr };
  CheckedPtr(const CheckedPtr&) noexcept = default;
  CheckedPtr(CheckedPtr&&) noexcept = default;
  CheckedPtr& operator=(const CheckedPtr&) noexcept = default;
  CheckedPtr& operator=(CheckedPtr&&) noexcept = default;

  ALWAYS_INLINE CheckedPtr& operator=(T* p) noexcept {
    wrapped_ptr_ = Impl::WrapRawPtr(p);
    return *this;
  }

  ~CheckedPtr() = default;

  // Avoid using. The goal of CheckedPtr is to be as close to raw pointer as
  // possible, so use it only if absolutely necessary (e.g. for const_cast).
  ALWAYS_INLINE T* get() const { return GetForExtraction(); }

  explicit ALWAYS_INLINE operator bool() const {
    return wrapped_ptr_ != Impl::GetWrappedNullPtr();
  }

  // Use SFINAE to avoid defining |operator*| for T=void, which wouldn't compile
  // due to |void&|.
  template <typename U = T,
            typename V = typename internal::DereferencedPointerType<U>::Type>
  ALWAYS_INLINE V& operator*() const {
    return *GetForDereference();
  }
  ALWAYS_INLINE T* operator->() const { return GetForDereference(); }
  // Deliberately implicit, because CheckedPtr is supposed to resemble raw ptr.
  // NOLINTNEXTLINE(runtime/explicit)
  ALWAYS_INLINE operator T*() const { return GetForExtraction(); }
  template <typename U>
  explicit ALWAYS_INLINE operator U*() const {
    return static_cast<U*>(GetForExtraction());
  }

  ALWAYS_INLINE CheckedPtr& operator++() {
    wrapped_ptr_ = Impl::Advance(wrapped_ptr_, sizeof(T));
    return *this;
  }

  ALWAYS_INLINE CheckedPtr& operator--() {
    wrapped_ptr_ = Impl::Advance(wrapped_ptr_, -sizeof(T));
    return *this;
  }

  ALWAYS_INLINE CheckedPtr& operator+=(ptrdiff_t delta_elems) {
    wrapped_ptr_ = Impl::Advance(wrapped_ptr_, delta_elems * sizeof(T));
    return *this;
  }

  ALWAYS_INLINE CheckedPtr& operator-=(ptrdiff_t delta_elems) {
    return *this += -delta_elems;
  }

  ALWAYS_INLINE bool operator==(T* p) const { return GetForComparison() == p; }
  ALWAYS_INLINE bool operator!=(T* p) const { return !operator==(p); }

  // Useful for cases like this:
  //   class Base {};
  //   class Derived : public Base {};
  //   Derived d;
  //   CheckedPtr<Derived> derived_ptr = &d;
  //   Base* base_ptr = &d;
  //   if (derived_ptr == base_ptr) {...}
  // Without these, such comparisons would end up calling |operator T*()|.
  template <typename U>
  ALWAYS_INLINE bool operator==(U* p) const {
    // Add |const| when casting, because |U| may have |const| in it. Even if |T|
    // doesn't, comparison between |T*| and |const T*| is fine.
    return GetForComparison() == static_cast<std::add_const_t<T>*>(p);
  }
  template <typename U>
  ALWAYS_INLINE bool operator!=(U* p) const {
    return !operator==(p);
  }

  ALWAYS_INLINE bool operator==(const CheckedPtr& other) const {
    return GetForComparison() == other.GetForComparison();
  }
  ALWAYS_INLINE bool operator!=(const CheckedPtr& other) const {
    return !operator==(other);
  }
  template <typename U, typename I>
  ALWAYS_INLINE bool operator==(const CheckedPtr<U, I>& other) const {
    // Add |const| when casting, because |U| may have |const| in it. Even if |T|
    // doesn't, comparison between |T*| and |const T*| is fine.
    return GetForComparison() ==
           static_cast<std::add_const_t<T>*>(other.GetForComparison());
  }
  template <typename U, typename I>
  ALWAYS_INLINE bool operator!=(const CheckedPtr<U, I>& other) const {
    return !operator==(other);
  }

  ALWAYS_INLINE void swap(CheckedPtr& other) noexcept {
    Impl::IncrementSwapCountForTest();
    std::swap(wrapped_ptr_, other.wrapped_ptr_);
  }

 private:
  // This getter is meant for situations where the pointer is meant to be
  // dereferenced. It is allowed to crash on nullptr (it may or may not),
  // because it knows that the caller will crash on nullptr.
  ALWAYS_INLINE T* GetForDereference() const {
    return static_cast<T*>(Impl::SafelyUnwrapPtrForDereference(wrapped_ptr_));
  }
  // This getter is meant for situations where the raw pointer is meant to be
  // extracted outside of this class, but not necessarily with an intention to
  // dereference. It mustn't crash on nullptr.
  ALWAYS_INLINE T* GetForExtraction() const {
    return static_cast<T*>(Impl::SafelyUnwrapPtrForExtraction(wrapped_ptr_));
  }
  // This getter is meant *only* for situations where the pointer is meant to be
  // compared (guaranteeing no dereference or extraction outside of this class).
  // Any verifications can and should be skipped for performance reasons.
  ALWAYS_INLINE T* GetForComparison() const {
    return static_cast<T*>(Impl::UnsafelyUnwrapPtrForComparison(wrapped_ptr_));
  }

  // Store the pointer as |uintptr_t|, because depending on implementation, its
  // unused bits may be re-purposed to store extra information.
  uintptr_t wrapped_ptr_;

  template <typename U, typename V>
  friend class CheckedPtr;
};

// These are for cases where a raw pointer is on the left hand side. Reverse
// order, so that |CheckedPtr::operator==()| kicks in, which will compare more
// efficiently. Otherwise the CheckedPtr operand would have to be cast to raw
// pointer, which may be more costly.
template <typename T, typename I>
ALWAYS_INLINE bool operator==(T* lhs, const CheckedPtr<T, I>& rhs) {
  return rhs == lhs;
}
template <typename T, typename I>
ALWAYS_INLINE bool operator!=(T* lhs, const CheckedPtr<T, I>& rhs) {
  return !operator==(lhs, rhs);
}
template <typename T, typename I, typename U>
ALWAYS_INLINE bool operator==(U* lhs, const CheckedPtr<T, I>& rhs) {
  return rhs == lhs;
}
template <typename T, typename I, typename U>
ALWAYS_INLINE bool operator!=(U* lhs, const CheckedPtr<T, I>& rhs) {
  return !operator==(lhs, rhs);
}

template <typename T, typename I>
ALWAYS_INLINE void swap(CheckedPtr<T, I>& lhs, CheckedPtr<T, I>& rhs) noexcept {
  lhs.swap(rhs);
}

}  // namespace base

using base::CheckedPtr;

#endif  // BASE_MEMORY_CHECKED_PTR_H_
