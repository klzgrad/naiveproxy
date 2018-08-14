// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/weak_ptr.h"

namespace base {
namespace internal {

WeakReference::Flag::Flag() : is_valid_(true) {}

void WeakReference::Flag::Invalidate() {
  is_valid_ = false;
}

bool WeakReference::Flag::IsValid() const {
  return is_valid_;
}

WeakReference::Flag::~Flag() = default;

WeakReference::WeakReference() = default;

WeakReference::WeakReference(const scoped_refptr<Flag>& flag) : flag_(flag) {}

WeakReference::~WeakReference() = default;

WeakReference::WeakReference(WeakReference&& other) = default;

WeakReference::WeakReference(const WeakReference& other) = default;

bool WeakReference::is_valid() const {
  return flag_ && flag_->IsValid();
}

WeakReferenceOwner::WeakReferenceOwner() = default;

WeakReferenceOwner::~WeakReferenceOwner() {
  Invalidate();
}

WeakReference WeakReferenceOwner::GetRef() const {
  // If we hold the last reference to the Flag then create a new one.
  if (!HasRefs())
    flag_ = new WeakReference::Flag();

  return WeakReference(flag_);
}

void WeakReferenceOwner::Invalidate() {
  if (flag_) {
    flag_->Invalidate();
    flag_ = nullptr;
  }
}

WeakPtrBase::WeakPtrBase() : ptr_(0) {}

WeakPtrBase::~WeakPtrBase() = default;

WeakPtrBase::WeakPtrBase(const WeakReference& ref, uintptr_t ptr)
    : ref_(ref), ptr_(ptr) {
  DCHECK(ptr_);
}

WeakPtrFactoryBase::WeakPtrFactoryBase(uintptr_t ptr) : ptr_(ptr) {
  DCHECK(ptr_);
}

WeakPtrFactoryBase::~WeakPtrFactoryBase() {
  ptr_ = 0;
}

}  // namespace internal
}  // namespace base
