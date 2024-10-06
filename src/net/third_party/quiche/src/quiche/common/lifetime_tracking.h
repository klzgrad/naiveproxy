// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements helper classes to track c++ object lifetimes. They are
// useful to debug use-after-free issues in environments where the cost of ASAN
// is too high.
//
// Suppose you have an object of type "MyClass" and a raw pointer "ptr" pointing
// to it, and you suspect a dereference of "ptr" is unsafe because the object it
// points to is dead. You can do
//
// (1) Add a LifetimeTrackable member to "MyClass". Alternatively, you can also
//     change MyClass to inherit from LifetimeTrackable.
//
//     struct MyClass {
//       ...... existing members ......
//       LifetimeTrackable trackable;
//     }
//
// (2) Add a LifetimeTracker alongside the "ptr".
//
//     ptr = new MyClass().
//     tracker = ptr->trackable.NewTracker();
//
// (3) Before the potentially dangerous dereference, check whether *ptr is dead:
//
//     if (tracker.IsTrackedObjectDead()) {
//       // ptr->trackable has been destructed. Log its destruction stack below.
//       QUICHE_LOG(ERROR) << "*ptr has bee destructed: " << tracker;
//     }
//     ptr->MethodCall();  // Possibly a use-after-free
//
// All classes defined in this file are not thread safe.

#ifndef QUICHE_COMMON_LIFETIME_TRACKING_H_
#define QUICHE_COMMON_LIFETIME_TRACKING_H_

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/strings/str_format.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_stack_trace.h"

namespace quiche {
namespace test {
class LifetimeTrackingTest;
}  // namespace test

// LifetimeInfo holds information about a LifetimeTrackable object.
struct QUICHE_EXPORT LifetimeInfo {
  bool IsDead() const { return destructor_stack.has_value(); }

  // If IsDead(), the stack when the LifetimeTrackable object is destructed.
  std::optional<std::vector<void*>> destructor_stack;
};

// LifetimeTracker tracks the lifetime of a LifetimeTrackable object, by holding
// a reference to its LifetimeInfo.
class QUICHE_EXPORT LifetimeTracker {
 public:
  // Copy constructor and assignment operator allow this tracker to track the
  // same object as |other|.
  LifetimeTracker(const LifetimeTracker& other) { CopyFrom(other); }
  LifetimeTracker& operator=(const LifetimeTracker& other) {
    CopyFrom(other);
    return *this;
  }

  // Move constructor and assignment are implemented as copies, to prevent the
  // moved-from object from tracking "nothing".
  LifetimeTracker(LifetimeTracker&& other) { CopyFrom(other); }
  LifetimeTracker& operator=(LifetimeTracker&& other) {
    CopyFrom(other);
    return *this;
  }

  // Whether the tracked object is dead.
  bool IsTrackedObjectDead() const { return info_->IsDead(); }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const LifetimeTracker& tracker) {
    if (tracker.info_->IsDead()) {
      absl::Format(&sink, "Tracked object has died with %v",
                   SymbolizeStackTrace(*tracker.info_->destructor_stack));
    } else {
      absl::Format(&sink, "Tracked object is alive.");
    }
  }

 private:
  friend class LifetimeTrackable;
  explicit LifetimeTracker(std::shared_ptr<const LifetimeInfo> info)
      : info_(std::move(info)) {
    QUICHE_CHECK(info_ != nullptr)
        << "Passed a null info pointer into the lifetime tracker";
  }
  void CopyFrom(const LifetimeTracker& other) { info_ = other.info_; }

  std::shared_ptr<const LifetimeInfo> info_;
};

// LifetimeTrackable allows its lifetime to be tracked by any number of
// LifetimeTracker(s).
class QUICHE_EXPORT LifetimeTrackable {
 public:
  LifetimeTrackable() = default;
  virtual ~LifetimeTrackable() {
    if (info_ != nullptr) {
      info_->destructor_stack = CurrentStackTrace();
    }
  }

  // LifetimeTrackable only tracks the memory occupied by itself. All copy/move
  // constructors and assignments are no-op.
  LifetimeTrackable(const LifetimeTrackable&) : LifetimeTrackable() {}
  LifetimeTrackable& operator=(const LifetimeTrackable&) { return *this; }
  LifetimeTrackable(LifetimeTrackable&&) : LifetimeTrackable() {}
  LifetimeTrackable& operator=(LifetimeTrackable&&) { return *this; }

  LifetimeTracker NewTracker() {
    if (info_ == nullptr) {
      info_ = std::make_shared<LifetimeInfo>();
    }
    return LifetimeTracker(info_);
  }

 private:
  friend class test::LifetimeTrackingTest;
  // nullptr if this object is not tracked by any LifetimeTracker.
  std::shared_ptr<LifetimeInfo> info_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_LIFETIME_TRACKING_H_
