// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_PROMISE_FINALLY_EXECUTOR_H_
#define BASE_TASK_PROMISE_FINALLY_EXECUTOR_H_

#include "base/task/promise/abstract_promise.h"
#include "base/task/promise/helpers.h"

namespace base {
namespace internal {

// Exists to reduce template bloat.
class BASE_EXPORT FinallyExecutorCommon {
 public:
  explicit FinallyExecutorCommon(CallbackBase&& callback) noexcept;
  ~FinallyExecutorCommon();

  // PromiseExecutor:
  bool IsCancelled() const;

  CallbackBase callback_;
};

// A finally promise executor runs regardless of whether the prerequisite was
// resolved or rejected. If the prerequsite is cancelled, the finally promise
// and any dependents are cancelled too.
template <typename CallbackT, typename ResolveStorage, typename RejectStorage>
class FinallyExecutor {
 public:
  using CallbackReturnT = typename CallbackTraits<CallbackT>::ReturnType;

  explicit FinallyExecutor(CallbackBase&& callback) noexcept
      : common_(std::move(callback)) {}

  ~FinallyExecutor() = default;

  bool IsCancelled() const { return common_.IsCancelled(); }

  PromiseExecutor::PrerequisitePolicy GetPrerequisitePolicy() const {
    return PromiseExecutor::PrerequisitePolicy::kAll;
  }

  void Execute(AbstractPromise* promise) {
    AbstractPromise* prerequisite = promise->GetOnlyPrerequisite();
    CallbackT* resolve_executor = static_cast<CallbackT*>(&common_.callback_);
    RunHelper<CallbackT, void, ResolveStorage, RejectStorage>::Run(
        std::move(*resolve_executor), prerequisite, promise);

    if (promise->IsResolvedWithPromise() ||
        promise->value().type() == TypeId::From<ResolveStorage>()) {
      promise->OnResolved();
    } else {
      DCHECK_EQ(promise->value().type(), TypeId::From<RejectStorage>());
      promise->OnRejected();
    }
  }

#if DCHECK_IS_ON()
  PromiseExecutor::ArgumentPassingType ResolveArgumentPassingType() const {
    return PromiseExecutor::ArgumentPassingType::kNormal;
  }

  PromiseExecutor::ArgumentPassingType RejectArgumentPassingType() const {
    return PromiseExecutor::ArgumentPassingType::kNormal;
  }

  bool CanResolve() const {
    return PromiseCallbackTraits<CallbackReturnT>::could_resolve;
  }

  bool CanReject() const {
    return PromiseCallbackTraits<CallbackReturnT>::could_reject;
  }
#endif

 private:
  FinallyExecutorCommon common_;
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_PROMISE_FINALLY_EXECUTOR_H_
