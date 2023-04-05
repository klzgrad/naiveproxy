// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_REFERENCE_COUNTED_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_REFERENCE_COUNTED_H_

#include "quiche_platform_impl/quiche_reference_counted_impl.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Base class for explicitly reference-counted objects in QUIC.
class QUICHE_EXPORT QuicheReferenceCounted : public QuicheReferenceCountedImpl {
 public:
  QuicheReferenceCounted() {}

 protected:
  ~QuicheReferenceCounted() override {}
};

// A class representing a reference counted pointer in QUIC.
//
// Construct or initialize QuicheReferenceCountedPointer from raw pointer. Here
// raw pointer MUST be a newly created object. Reference count of a newly
// created object is undefined, but that will be 1 after being added to
// QuicheReferenceCountedPointer.
// QuicheReferenceCountedPointer is used as a local variable.
// QuicheReferenceCountedPointer<T> r_ptr(new T());
// or, equivalently:
// QuicheReferenceCountedPointer<T> r_ptr;
// T* p = new T();
// r_ptr = T;
//
// QuicheReferenceCountedPointer is used as a member variable:
// MyClass::MyClass() : r_ptr(new T()) {}
//
// This is WRONG, since *p is not guaranteed to be newly created:
// MyClass::MyClass(T* p) : r_ptr(p) {}
//
// Given an existing QuicheReferenceCountedPointer, create a duplicate that has
// its own reference on the object:
// QuicheReferenceCountedPointer<T> r_ptr_b(r_ptr_a);
// or, equivalently:
// QuicheReferenceCountedPointer<T> r_ptr_b = r_ptr_a;
//
// Given an existing QuicheReferenceCountedPointer, create a
// QuicheReferenceCountedPointer that adopts the reference:
// QuicheReferenceCountedPointer<T> r_ptr_b(std::move(r_ptr_a));
// or, equivalently:
// QuicheReferenceCountedPointer<T> r_ptr_b = std::move(r_ptr_a);

template <class T>
class QUICHE_NO_EXPORT QuicheReferenceCountedPointer {
 public:
  QuicheReferenceCountedPointer() = default;

  // Constructor from raw pointer |p|. This guarantees that the reference count
  // of *p is 1. This should be only called when a new object is created.
  // Calling this on an already existent object does not increase its reference
  // count.
  explicit QuicheReferenceCountedPointer(T* p) : impl_(p) {}

  // Allows implicit conversion from nullptr.
  QuicheReferenceCountedPointer(std::nullptr_t) : impl_(nullptr) {}  // NOLINT

  // Copy and copy conversion constructors. It does not take the reference away
  // from |other| and they each end up with their own reference.
  template <typename U>
  QuicheReferenceCountedPointer(  // NOLINT
      const QuicheReferenceCountedPointer<U>& other)
      : impl_(other.impl()) {}
  QuicheReferenceCountedPointer(const QuicheReferenceCountedPointer& other)
      : impl_(other.impl()) {}

  // Move constructors. After move, it adopts the reference from |other|.
  template <typename U>
  QuicheReferenceCountedPointer(
      QuicheReferenceCountedPointer<U>&& other)  // NOLINT
      : impl_(std::move(other.impl())) {}
  QuicheReferenceCountedPointer(QuicheReferenceCountedPointer&& other)
      : impl_(std::move(other.impl())) {}

  ~QuicheReferenceCountedPointer() = default;

  // Copy assignments.
  QuicheReferenceCountedPointer& operator=(
      const QuicheReferenceCountedPointer& other) {
    impl_ = other.impl();
    return *this;
  }
  template <typename U>
  QuicheReferenceCountedPointer<T>& operator=(
      const QuicheReferenceCountedPointer<U>& other) {
    impl_ = other.impl();
    return *this;
  }

  // Move assignments.
  QuicheReferenceCountedPointer& operator=(
      QuicheReferenceCountedPointer&& other) {
    impl_ = std::move(other.impl());
    return *this;
  }
  template <typename U>
  QuicheReferenceCountedPointer<T>& operator=(
      QuicheReferenceCountedPointer<U>&& other) {
    impl_ = std::move(other.impl());
    return *this;
  }

  // Accessors for the referenced object.
  // operator*() and operator->() will assert() if there is no current object.
  T& operator*() const { return *impl_; }
  T* operator->() const { return impl_.get(); }

  explicit operator bool() const { return static_cast<bool>(impl_); }

  // Assignment operator on raw pointer. Drops a reference to current pointee,
  // if any, and replaces it with |p|. This guarantees that the reference count
  // of *p is 1. This should only be used when a new object is created.  Calling
  // this on an already existent object is undefined behavior.
  QuicheReferenceCountedPointer<T>& operator=(T* p) {
    impl_ = p;
    return *this;
  }

  // Returns the raw pointer with no change in reference count.
  T* get() const { return impl_.get(); }

  QuicheReferenceCountedPointerImpl<T>& impl() { return impl_; }
  const QuicheReferenceCountedPointerImpl<T>& impl() const { return impl_; }

  // Comparisons against same type.
  friend bool operator==(const QuicheReferenceCountedPointer& a,
                         const QuicheReferenceCountedPointer& b) {
    return a.get() == b.get();
  }
  friend bool operator!=(const QuicheReferenceCountedPointer& a,
                         const QuicheReferenceCountedPointer& b) {
    return a.get() != b.get();
  }

  // Comparisons against nullptr.
  friend bool operator==(const QuicheReferenceCountedPointer& a,
                         std::nullptr_t) {
    return a.get() == nullptr;
  }
  friend bool operator==(std::nullptr_t,
                         const QuicheReferenceCountedPointer& b) {
    return nullptr == b.get();
  }
  friend bool operator!=(const QuicheReferenceCountedPointer& a,
                         std::nullptr_t) {
    return a.get() != nullptr;
  }
  friend bool operator!=(std::nullptr_t,
                         const QuicheReferenceCountedPointer& b) {
    return nullptr != b.get();
  }

 private:
  QuicheReferenceCountedPointerImpl<T> impl_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_REFERENCE_COUNTED_H_
