// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_REFERENCE_COUNTED_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_REFERENCE_COUNTED_IMPL_H_

#include <atomic>
#include <memory>

#include "absl/base/attributes.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {

class QUICHE_EXPORT QuicheReferenceCountedImpl {
 public:
  virtual ~QuicheReferenceCountedImpl() { QUICHE_DCHECK_EQ(ref_count_, 0); }

  void AddReference() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

  // Returns true if the objects needs to be deleted.
  ABSL_MUST_USE_RESULT bool RemoveReference() {
    int new_count = ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    QUICHE_DCHECK_GE(new_count, 0);
    return new_count == 0;
  }

  bool HasUniqueReference() const {
    return ref_count_.load(std::memory_order_acquire) == 1;
  }

 private:
  std::atomic<int> ref_count_ = 1;
};

template <class T>
class QUICHE_NO_EXPORT QuicheReferenceCountedPointerImpl {
 public:
  QuicheReferenceCountedPointerImpl() = default;
  ~QuicheReferenceCountedPointerImpl() { RemoveReference(); }

  // Constructor from raw pointer |p|. This guarantees that the reference count
  // of *p is 1. This should be only called when a new object is created.
  explicit QuicheReferenceCountedPointerImpl(T* p) : object_(p) {
    if (p != nullptr) {
      QUICHE_DCHECK(p->HasUniqueReference());
    }
  }

  explicit QuicheReferenceCountedPointerImpl(std::nullptr_t)
      : object_(nullptr) {}

  // Copy and copy conversion constructors.
  QuicheReferenceCountedPointerImpl(
      const QuicheReferenceCountedPointerImpl& other) {
    AssignObject(other.get());
    AddReference();
  }
  template <typename U>
  QuicheReferenceCountedPointerImpl(  // NOLINT
      const QuicheReferenceCountedPointerImpl<U>& other) {
    AssignObject(other.get());
    AddReference();
  }

  // Move constructors.
  QuicheReferenceCountedPointerImpl(QuicheReferenceCountedPointerImpl&& other) {
    object_ = other.object_;
    other.object_ = nullptr;
  }
  template <typename U>
  QuicheReferenceCountedPointerImpl(
      QuicheReferenceCountedPointerImpl<U>&& other) {  // NOLINT
    // We can't access other.object_ since other has different T and object_ is
    // private.
    object_ = other.get();
    AddReference();
    other = nullptr;
  }

  // Copy assignments.
  QuicheReferenceCountedPointerImpl& operator=(
      const QuicheReferenceCountedPointerImpl& other) {
    AssignObject(other.object_);
    AddReference();
    return *this;
  }
  template <typename U>
  QuicheReferenceCountedPointerImpl<T>& operator=(
      const QuicheReferenceCountedPointerImpl<U>& other) {
    AssignObject(other.object_);
    AddReference();
    return *this;
  }

  // Move assignments.
  QuicheReferenceCountedPointerImpl& operator=(
      QuicheReferenceCountedPointerImpl&& other) {
    AssignObject(other.object_);
    other.object_ = nullptr;
    return *this;
  }
  template <typename U>
  QuicheReferenceCountedPointerImpl<T>& operator=(
      QuicheReferenceCountedPointerImpl<U>&& other) {
    AssignObject(other.get());
    AddReference();
    other = nullptr;
    return *this;
  }

  T& operator*() const { return *object_; }
  T* operator->() const { return object_; }

  explicit operator bool() const { return object_ != nullptr; }

  // Assignment operator on raw pointer.  Behaves similar to the raw pointer
  // constructor.
  QuicheReferenceCountedPointerImpl<T>& operator=(T* p) {
    AssignObject(p);
    if (p != nullptr) {
      QUICHE_DCHECK(p->HasUniqueReference());
    }
    return *this;
  }

  // Returns the raw pointer with no change in reference count.
  T* get() const { return object_; }

  // Comparisons against same type.
  friend bool operator==(const QuicheReferenceCountedPointerImpl& a,
                         const QuicheReferenceCountedPointerImpl& b) {
    return a.get() == b.get();
  }
  friend bool operator!=(const QuicheReferenceCountedPointerImpl& a,
                         const QuicheReferenceCountedPointerImpl& b) {
    return a.get() != b.get();
  }

  // Comparisons against nullptr.
  friend bool operator==(const QuicheReferenceCountedPointerImpl& a,
                         std::nullptr_t) {
    return a.get() == nullptr;
  }
  friend bool operator==(std::nullptr_t,
                         const QuicheReferenceCountedPointerImpl& b) {
    return nullptr == b.get();
  }
  friend bool operator!=(const QuicheReferenceCountedPointerImpl& a,
                         std::nullptr_t) {
    return a.get() != nullptr;
  }
  friend bool operator!=(std::nullptr_t,
                         const QuicheReferenceCountedPointerImpl& b) {
    return nullptr != b.get();
  }

 private:
  void AddReference() {
    if (object_ == nullptr) {
      return;
    }
    QuicheReferenceCountedImpl* implicitly_cast_object = object_;
    implicitly_cast_object->AddReference();
  }

  void RemoveReference() {
    if (object_ == nullptr) {
      return;
    }
    QuicheReferenceCountedImpl* implicitly_cast_object = object_;
    if (implicitly_cast_object->RemoveReference()) {
      delete implicitly_cast_object;
    }
    object_ = nullptr;
  }

  void AssignObject(T* new_object) {
    RemoveReference();
    object_ = new_object;
  }

  T* object_ = nullptr;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_REFERENCE_COUNTED_IMPL_H_
