// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CALLBACK_H_
#define BASE_CALLBACK_H_

#include "base/callback_forward.h"
#include "base/callback_internal.h"

// NOTE: Header files that do not require the full definition of Callback or
// Closure should #include "base/callback_forward.h" instead of this file.

// -----------------------------------------------------------------------------
// Usage documentation
// -----------------------------------------------------------------------------
//
// See //docs/callback.md for documentation.

namespace base {

template <typename R, typename... Args>
class OnceCallback<R(Args...)> : public internal::CallbackBase {
 public:
  using RunType = R(Args...);
  using PolymorphicInvoke = R (*)(internal::BindStateBase*,
                                  internal::PassingTraitsType<Args>...);

  OnceCallback() : internal::CallbackBase(nullptr) {}

  explicit OnceCallback(internal::BindStateBase* bind_state)
      : internal::CallbackBase(bind_state) {}

  OnceCallback(const OnceCallback&) = delete;
  OnceCallback& operator=(const OnceCallback&) = delete;

  OnceCallback(OnceCallback&&) = default;
  OnceCallback& operator=(OnceCallback&&) = default;

  OnceCallback(RepeatingCallback<RunType> other)
      : internal::CallbackBase(std::move(other)) {}

  OnceCallback& operator=(RepeatingCallback<RunType> other) {
    static_cast<internal::CallbackBase&>(*this) = std::move(other);
    return *this;
  }

  bool Equals(const OnceCallback& other) const { return EqualsInternal(other); }

  R Run(Args... args) const & {
    static_assert(!sizeof(*this),
                  "OnceCallback::Run() may only be invoked on a non-const "
                  "rvalue, i.e. std::move(callback).Run().");
    NOTREACHED();
  }

  R Run(Args... args) && {
    // Move the callback instance into a local variable before the invocation,
    // that ensures the internal state is cleared after the invocation.
    // It's not safe to touch |this| after the invocation, since running the
    // bound function may destroy |this|.
    OnceCallback cb = std::move(*this);
    PolymorphicInvoke f =
        reinterpret_cast<PolymorphicInvoke>(cb.polymorphic_invoke());
    return f(cb.bind_state_.get(), std::forward<Args>(args)...);
  }
};

template <typename R, typename... Args>
class RepeatingCallback<R(Args...)> : public internal::CallbackBaseCopyable {
 public:
  using RunType = R(Args...);
  using PolymorphicInvoke = R (*)(internal::BindStateBase*,
                                  internal::PassingTraitsType<Args>...);

  RepeatingCallback() : internal::CallbackBaseCopyable(nullptr) {}

  explicit RepeatingCallback(internal::BindStateBase* bind_state)
      : internal::CallbackBaseCopyable(bind_state) {}

  // Copyable and movabl.
  RepeatingCallback(const RepeatingCallback&) = default;
  RepeatingCallback& operator=(const RepeatingCallback&) = default;
  RepeatingCallback(RepeatingCallback&&) = default;
  RepeatingCallback& operator=(RepeatingCallback&&) = default;

  bool Equals(const RepeatingCallback& other) const {
    return EqualsInternal(other);
  }

  R Run(Args... args) const & {
    PolymorphicInvoke f =
        reinterpret_cast<PolymorphicInvoke>(this->polymorphic_invoke());
    return f(this->bind_state_.get(), std::forward<Args>(args)...);
  }

  R Run(Args... args) && {
    // Move the callback instance into a local variable before the invocation,
    // that ensures the internal state is cleared after the invocation.
    // It's not safe to touch |this| after the invocation, since running the
    // bound function may destroy |this|.
    RepeatingCallback cb = std::move(*this);
    PolymorphicInvoke f =
        reinterpret_cast<PolymorphicInvoke>(cb.polymorphic_invoke());
    return f(cb.bind_state_.get(), std::forward<Args>(args)...);
  }
};

}  // namespace base

#endif  // BASE_CALLBACK_H_
