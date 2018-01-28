// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_REFERENCE_COUNTED_H_
#define NET_QUIC_PLATFORM_API_QUIC_REFERENCE_COUNTED_H_

#include "net/quic/platform/impl/quic_reference_counted_impl.h"

namespace net {

// Base class for explicitly reference-counted objects in QUIC.
class QUIC_EXPORT_PRIVATE QuicReferenceCounted
    : public QuicReferenceCountedImpl {
 public:
  QuicReferenceCounted() {}

 protected:
  ~QuicReferenceCounted() override {}
};

// A class representing a reference counted pointer in QUIC.
//
// Construct or initialize QuicReferenceCountedPointer from raw pointer. Here
// raw pointer MUST be a newly created object. Reference count of a newly
// created object is undefined, but that will be 1 after being added to
// QuicReferenceCountedPointer.
// QuicReferenceCountedPointer is used as a local variable.
// QuicReferenceCountedPointer<T> r_ptr(new T());
// or, equivalently:
// QuicReferenceCountedPointer<T> r_ptr;
// T* p = new T();
// r_ptr = T;
//
// QuicReferenceCountedPointer is used as a member variable:
// MyClass::MyClass() : r_ptr(new T()) {}
//
// This is WRONG, since *p is not guaranteed to be newly created:
// MyClass::MyClass(T* p) : r_ptr(p) {}
//
// Given an existing QuicReferenceCountedPointer, create a duplicate that has
// its own reference on the object:
// QuicReferenceCountedPointer<T> r_ptr_b(r_ptr_a);
// or, equivalently:
// QuicReferenceCountedPointer<T> r_ptr_b = r_ptr_a;
//
// Given an existing QuicReferenceCountedPointer, create a
// QuicReferenceCountedPointer that adopts the reference:
// QuicReferenceCountedPointer<T> r_ptr_b(std::move(r_ptr_a));
// or, equivalently:
// QuicReferenceCountedPointer<T> r_ptr_b = std::move(r_ptr_a);

template <class T>
class QuicReferenceCountedPointer {
 public:
  QuicReferenceCountedPointer() = default;

  // Constructor from raw pointer |p|. This guarantees the reference count of *p
  // is 1. This should only be used when a new object is created, calling this
  // on an already existent object is undefined behavior.
  explicit QuicReferenceCountedPointer(T* p) : impl_(p) {}

  // Allows implicit conversion from nullptr.
  QuicReferenceCountedPointer(std::nullptr_t) : impl_(nullptr) {}  // NOLINT

  // Copy and copy conversion constructors. It does not take the reference away
  // from |other| and they each end up with their own reference.
  template <typename U>
  QuicReferenceCountedPointer(  // NOLINT
      const QuicReferenceCountedPointer<U>& other)
      : impl_(other.impl()) {}
  QuicReferenceCountedPointer(const QuicReferenceCountedPointer& other)
      : impl_(other.impl()) {}

  // Move constructors. After move, It adopts the reference from |other|.
  template <typename U>
  QuicReferenceCountedPointer(QuicReferenceCountedPointer<U>&& other)  // NOLINT
      : impl_(std::move(other.impl())) {}
  QuicReferenceCountedPointer(QuicReferenceCountedPointer&& other)
      : impl_(std::move(other.impl())) {}

  ~QuicReferenceCountedPointer() = default;

  // Copy assignments.
  QuicReferenceCountedPointer& operator=(
      const QuicReferenceCountedPointer& other) {
    impl_ = other.impl();
    return *this;
  }
  template <typename U>
  QuicReferenceCountedPointer<T>& operator=(
      const QuicReferenceCountedPointer<U>& other) {
    impl_ = other.impl();
    return *this;
  }

  // Move assignments.
  QuicReferenceCountedPointer& operator=(QuicReferenceCountedPointer&& other) {
    impl_ = std::move(other.impl());
    return *this;
  }
  template <typename U>
  QuicReferenceCountedPointer<T>& operator=(
      QuicReferenceCountedPointer<U>&& other) {
    impl_ = std::move(other.impl());
    return *this;
  }

  // Accessors for the referenced object.
  // operator* and operator-> will assert() if there is no current object.
  T& operator*() const { return *impl_; }
  T* operator->() const { return impl_.get(); }

  explicit operator bool() const { return static_cast<bool>(impl_); }

  // Assignment operator on raw pointer. Drops a reference to current pointee,
  // if any and replaces it with |p|. This garantee the reference count of *p is
  // 1. This should only be used when a new object is created, calling this
  // on a already existent object is undefined behavior.
  QuicReferenceCountedPointer<T>& operator=(T* p) {
    impl_ = p;
    return *this;
  }

  // Returns the raw pointer with no change in reference.
  T* get() const { return impl_.get(); }

  QuicReferenceCountedPointerImpl<T>& impl() { return impl_; }
  const QuicReferenceCountedPointerImpl<T>& impl() const { return impl_; }

  // Comparisons against same type.
  friend bool operator==(const QuicReferenceCountedPointer& a,
                         const QuicReferenceCountedPointer& b) {
    return a.get() == b.get();
  }
  friend bool operator!=(const QuicReferenceCountedPointer& a,
                         const QuicReferenceCountedPointer& b) {
    return a.get() != b.get();
  }

  // Comparisons against nullptr.
  friend bool operator==(const QuicReferenceCountedPointer& a, std::nullptr_t) {
    return a.get() == nullptr;
  }
  friend bool operator==(std::nullptr_t, const QuicReferenceCountedPointer& b) {
    return nullptr == b.get();
  }
  friend bool operator!=(const QuicReferenceCountedPointer& a, std::nullptr_t) {
    return a.get() != nullptr;
  }
  friend bool operator!=(std::nullptr_t, const QuicReferenceCountedPointer& b) {
    return nullptr != b.get();
  }

 private:
  QuicReferenceCountedPointerImpl<T> impl_;
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_REFERENCE_COUNTED_H_
