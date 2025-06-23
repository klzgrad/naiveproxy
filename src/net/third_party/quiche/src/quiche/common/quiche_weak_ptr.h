// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The API in this header provides ability for objects to create weak pointers
// to themselves.  Unlike a regular pointer, a weak pointer is aware as to
// whether the object it points to is still alive or has been deleted.  Unlike
// std::weak_ptr, QuicheWeakPtr does not require the referred object to be owned
// by an std::shared_ptr, and is not thread-safe.  Conceptually, this is similar
// to base::WeakPtrFactory in Chromium, though the API and the implementation
// are dramatically simplified.
//
// Example usage:
//
//   class MyClass {
//    public:
//     void PerformAsyncOperation() {
//       ScheduleOperation([weak_this = weak_factory_.Create()] {
//         MyClass* self = weak_this.GetIfAvaliable();
//         if (self == nullptr) {
//           return;
//         }
//         self->OnOperationComplete();
//       });
//     }
//
//    private:
//     quiche::QuicheWeakPtrFactory<MyClass> weak_factory_;  // Must be last
//   };

#ifndef QUICHE_COMMON_QUICHE_WEAK_PTR_H_
#define QUICHE_COMMON_QUICHE_WEAK_PTR_H_

#include <cstdint>
#include <memory>

#include "absl/base/nullability.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quiche {

template <typename T>
class QuicheWeakPtrFactory;

// QuicheWeakPtr contains a pointer to an object that may or may not be alive,
// or nullptr.
template <typename T>
class QUICHE_NO_EXPORT QuicheWeakPtr final {
 public:
  // Initializes a null weak pointer.
  QuicheWeakPtr() = default;

  // Returns the pointer to the underlying object if it is alive, or nullptr
  // otherwise.
  T* /*absl_nullable*/ GetIfAvailable() const {
    return control_block_ != nullptr ? control_block_->Get() : nullptr;
  }

  // Returns true if the underlying object is alive.
  bool IsValid() const {
    return control_block_ != nullptr ? control_block_->IsValid() : false;
  }

 private:
  friend class QuicheWeakPtrFactory<T>;

  // An object shared by all of the weak pointers pointing to a given object.
  // Initially it points to the object itself; when the object is destryoed, the
  // contained pointer is set to nullptr.
  class ControlBlock {
   public:
    explicit ControlBlock(T* /*absl_nonnull*/ object) : object_(object) {}

    T* /*absl_nullable*/ Get() const { return object_; }
    void Clear() { object_ = nullptr; }
    bool IsValid() const { return object_ != nullptr; }

   private:
    T* /*absl_nullable*/ object_;
  };

  explicit QuicheWeakPtr(std::shared_ptr<ControlBlock> block)
      : control_block_(std::move(block)) {}

  /*absl_nullable*/ std::shared_ptr<ControlBlock> control_block_ = nullptr;
};

// QuicheWeakPtrFactory generates weak pointers to the parent object, and cleans
// up the state when the parent object is destroyed.  In order to do that
// correctly, it MUST be the last field in the object holding it.
template <typename T>
class QUICHE_NO_EXPORT QuicheWeakPtrFactory final {
 public:
  explicit QuicheWeakPtrFactory(T* /*absl_nonnull*/ object)
      : control_block_(std::make_shared<ControlBlock>(object)) {}
  ~QuicheWeakPtrFactory() { control_block_->Clear(); }

  QuicheWeakPtrFactory(const QuicheWeakPtrFactory&) = delete;
  QuicheWeakPtrFactory& operator=(const QuicheWeakPtrFactory&) = delete;

  // QuicheWeakPtrFactory is attached to the parent object; moving either the
  // parent object or the factory would almost certainly be a mistake, since all
  // of the existing WeakPtrs would point to the old object.
  QuicheWeakPtrFactory(QuicheWeakPtrFactory&&) = delete;
  QuicheWeakPtrFactory& operator=(QuicheWeakPtrFactory&&) = delete;

  // Creates a weak pointer to the parent object.
  QuicheWeakPtr<T> Create() const { return QuicheWeakPtr<T>(control_block_); }

 private:
  using ControlBlock = typename QuicheWeakPtr<T>::ControlBlock;
  /*absl_nonnull*/ std::shared_ptr<ControlBlock> control_block_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_WEAK_PTR_H_
