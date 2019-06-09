// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_PROMISE_THEN_AND_CATCH_EXECUTOR_H_
#define BASE_TASK_PROMISE_THEN_AND_CATCH_EXECUTOR_H_

#include <type_traits>

#include "base/callback.h"
#include "base/task/promise/abstract_promise.h"
#include "base/task/promise/helpers.h"

namespace base {
namespace internal {

// Exists to reduce template bloat.
class BASE_EXPORT ThenAndCatchExecutorCommon {
 public:
  ThenAndCatchExecutorCommon(CallbackBase&& resolve_callback,
                             CallbackBase&& reject_callback);

  ~ThenAndCatchExecutorCommon();

  // AbstractPromise::Executor:
  bool IsCancelled() const;
  AbstractPromise::Executor::PrerequisitePolicy GetPrerequisitePolicy() const;

  using ExecuteCallback = void (*)(AbstractPromise* prerequisite,
                                   AbstractPromise* promise,
                                   ThenAndCatchExecutorCommon* executor);

  void Execute(AbstractPromise* promise,
               ExecuteCallback execute_then,
               ExecuteCallback execute_catch);

  // If |executor| is null then the value of |arg| is moved or copied into
  // |result| and true is returned. Otherwise false is returned.
  static bool ProcessNullCallback(const CallbackBase& executor,
                                  AbstractPromise* arg,
                                  AbstractPromise* result);

  CallbackBase resolve_callback_;
  CallbackBase reject_callback_;
};

// Tag signals no callback which is used to eliminate dead code.
struct NoCallback {};

struct CouldResolveOrReject {};
struct CanOnlyResolve {};
struct CanOnlyReject {};

template <bool can_resolve, bool can_reject>
struct CheckResultHelper;

template <>
struct CheckResultHelper<true, false> {
  using TagType = CanOnlyResolve;
};

template <>
struct CheckResultHelper<true, true> {
  using TagType = CouldResolveOrReject;
};

template <>
struct CheckResultHelper<false, true> {
  using TagType = CanOnlyReject;
};

template <typename ResolveOnceCallback,
          typename RejectOnceCallback,
          typename ArgResolve,
          typename ArgReject,
          typename ResolveStorage,
          typename RejectStorage>
class ThenAndCatchExecutor {
 public:
  using ResolveReturnT =
      typename CallbackTraits<ResolveOnceCallback>::ReturnType;
  using RejectReturnT = typename CallbackTraits<RejectOnceCallback>::ReturnType;
  using PrerequisiteCouldResolve =
      std::integral_constant<bool,
                             !std::is_same<ArgResolve, NoCallback>::value>;
  using PrerequisiteCouldReject =
      std::integral_constant<bool, !std::is_same<ArgReject, NoCallback>::value>;

  ThenAndCatchExecutor(ResolveOnceCallback&& resolve_callback,
                       RejectOnceCallback&& reject_callback)
      : common_(std::move(resolve_callback), std::move(reject_callback)) {
    static_assert(sizeof(CallbackBase) == sizeof(ResolveOnceCallback),
                  "We assume it's possible to cast from CallbackBase to "
                  "ResolveOnceCallback");
    static_assert(sizeof(CallbackBase) == sizeof(RejectOnceCallback),
                  "We assume it's possible to cast from CallbackBase to "
                  "RejectOnceCallback");
  }

  bool IsCancelled() const { return common_.IsCancelled(); }

  AbstractPromise::Executor::PrerequisitePolicy GetPrerequisitePolicy() const {
    return common_.GetPrerequisitePolicy();
  }

  using ExecuteCallback = ThenAndCatchExecutorCommon::ExecuteCallback;

  void Execute(AbstractPromise* promise) {
    return common_.Execute(promise, &ExecuteThen, &ExecuteCatch);
  }

#if DCHECK_IS_ON()
  AbstractPromise::Executor::ArgumentPassingType ResolveArgumentPassingType()
      const {
    return common_.resolve_callback_.is_null()
               ? AbstractPromise::Executor::ArgumentPassingType::kNoCallback
               : CallbackTraits<ResolveOnceCallback>::argument_passing_type;
  }

  AbstractPromise::Executor::ArgumentPassingType RejectArgumentPassingType()
      const {
    return common_.reject_callback_.is_null()
               ? AbstractPromise::Executor::ArgumentPassingType::kNoCallback
               : CallbackTraits<RejectOnceCallback>::argument_passing_type;
  }

  bool CanResolve() const {
    return (!common_.resolve_callback_.is_null() &&
            PromiseCallbackTraits<ResolveReturnT>::could_resolve) ||
           (!common_.reject_callback_.is_null() &&
            PromiseCallbackTraits<RejectReturnT>::could_resolve);
  }

  bool CanReject() const {
    return (!common_.resolve_callback_.is_null() &&
            PromiseCallbackTraits<ResolveReturnT>::could_reject) ||
           (!common_.reject_callback_.is_null() &&
            PromiseCallbackTraits<RejectReturnT>::could_reject);
  }
#endif

 private:
  static void ExecuteThen(AbstractPromise* prerequisite,
                          AbstractPromise* promise,
                          ThenAndCatchExecutorCommon* common) {
    ExecuteThenInternal(prerequisite, promise, common,
                        PrerequisiteCouldResolve());
  }

  static void ExecuteCatch(AbstractPromise* prerequisite,
                           AbstractPromise* promise,
                           ThenAndCatchExecutorCommon* common) {
    ExecuteCatchInternal(prerequisite, promise, common,
                         PrerequisiteCouldReject());
  }

  static void ExecuteThenInternal(AbstractPromise* prerequisite,
                                  AbstractPromise* promise,
                                  ThenAndCatchExecutorCommon* common,
                                  std::true_type can_resolve) {
    ResolveOnceCallback* resolve_callback =
        static_cast<ResolveOnceCallback*>(&common->resolve_callback_);
    RunHelper<ResolveOnceCallback, Resolved<ArgResolve>, ResolveStorage,
              RejectStorage>::Run(std::move(*resolve_callback), prerequisite,
                                  promise);

    using CheckResultTagType = typename CheckResultHelper<
        PromiseCallbackTraits<ResolveReturnT>::could_resolve,
        PromiseCallbackTraits<ResolveReturnT>::could_reject>::TagType;

    CheckResultType(promise, CheckResultTagType());
  }

  static void ExecuteThenInternal(AbstractPromise* prerequisite,
                                  AbstractPromise* promise,
                                  ThenAndCatchExecutorCommon* common,
                                  std::false_type can_resolve) {
    // |prerequisite| can't resolve so don't generate dead code.
  }

  static void ExecuteCatchInternal(AbstractPromise* prerequisite,
                                   AbstractPromise* promise,
                                   ThenAndCatchExecutorCommon* common,
                                   std::true_type can_reject) {
    RejectOnceCallback* reject_callback =
        static_cast<RejectOnceCallback*>(&common->reject_callback_);
    RunHelper<RejectOnceCallback, Rejected<ArgReject>, ResolveStorage,
              RejectStorage>::Run(std::move(*reject_callback), prerequisite,
                                  promise);

    using CheckResultTagType = typename CheckResultHelper<
        PromiseCallbackTraits<RejectReturnT>::could_resolve,
        PromiseCallbackTraits<RejectReturnT>::could_reject>::TagType;

    CheckResultType(promise, CheckResultTagType());
  }

  static void ExecuteCatchInternal(AbstractPromise* prerequisite,
                                   AbstractPromise* promise,
                                   ThenAndCatchExecutorCommon* common,
                                   std::false_type can_reject) {
    // |prerequisite| can't reject so don't generate dead code.
  }

  static void CheckResultType(AbstractPromise* promise, CouldResolveOrReject) {
    if (promise->IsResolvedWithPromise() ||
        promise->value().type() == TypeId::From<ResolveStorage>()) {
      promise->OnResolved();
    } else {
      DCHECK_EQ(promise->value().type(), TypeId::From<RejectStorage>())
          << " See " << promise->from_here().ToString();
      promise->OnRejected();
    }
  }

  static void CheckResultType(AbstractPromise* promise, CanOnlyResolve) {
    promise->OnResolved();
  }

  static void CheckResultType(AbstractPromise* promise, CanOnlyReject) {
    promise->OnRejected();
  }

  ThenAndCatchExecutorCommon common_;
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_PROMISE_THEN_AND_CATCH_EXECUTOR_H_
