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

#ifndef SRC_PROTOVM_OWNED_PTR_H_
#define SRC_PROTOVM_OWNED_PTR_H_

#include "perfetto/base/logging.h"

namespace perfetto {
namespace protovm {

// OwnedPtr is essentially a std::unique_ptr that doesn't perform any deletion
// itself. Its purpose is to clearly express ownership semantics for objects
// that are managed with a custom allocators.
//
// It is not a unique_ptr, because that's not guaranteed to be standard layout
//
// OwnedPtr is extensively used within RwProto to manage the lifecycle and
// clarify ownership of the internal, manually-allocated nodes.
template <class T>
class OwnedPtr {
 public:
  using pointer = T*;
  OwnedPtr(const OwnedPtr&) = delete;
  OwnedPtr(OwnedPtr&& other) {
    p_ = other.p_;
    other.p_ = nullptr;
  }
  OwnedPtr(T* p) noexcept : p_(p) {}  // NOLINT: explicit
  ~OwnedPtr() noexcept { reset(); }
  OwnedPtr& operator=(const OwnedPtr&) = delete;
  OwnedPtr& operator=(OwnedPtr&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    reset();
    p_ = other.p_;
    other.p_ = nullptr;
    return *this;
  }
  T* get() const noexcept { return p_; }
  T* release() noexcept {
    T* p = p_;
    p_ = nullptr;
    return p;
  }
  typename std::add_lvalue_reference<T>::type operator*() const noexcept {
    return *p_;
  }
  explicit operator bool() const noexcept { return get() != nullptr; }
  T* operator->() const noexcept { return p_; }
  void reset(T* p = pointer()) noexcept {
    PERFETTO_DCHECK(p_ == nullptr);
    p_ = p;
  }

 private:
  T* p_ = nullptr;
};

}  // namespace protovm
}  // namespace perfetto

#endif  // SRC_PROTOVM_OWNED_PTR_H_
