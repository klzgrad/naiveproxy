// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/promise/helpers.h"

#include "base/bind_helpers.h"
#include "base/task/promise/no_op_promise_executor.h"
#include "base/threading/sequenced_task_runner_handle.h"

namespace base {
namespace internal {

PromiseHolder::PromiseHolder(scoped_refptr<internal::AbstractPromise> promise)
    : promise_(std::move(promise)) {}

PromiseHolder::~PromiseHolder() {
  // Detect if the promise was not executed and if so cancel to ensure memory
  // is released.
  if (promise_)
    promise_->OnCanceled();
}

PromiseHolder::PromiseHolder(PromiseHolder&& other)
    : promise_(std::move(other.promise_)) {}

scoped_refptr<internal::AbstractPromise> PromiseHolder::Unwrap() const {
  return std::move(promise_);
}

scoped_refptr<TaskRunner> GetCurrentSequence() {
  return SequencedTaskRunnerHandle::Get();
}

DoNothing ToCallbackBase(DoNothing task) {
  return task;
}

scoped_refptr<AbstractPromise> ConstructAbstractPromiseWithSinglePrerequisite(
    const scoped_refptr<TaskRunner>& task_runner,
    const Location& from_here,
    AbstractPromise* prerequsite,
    internal::PromiseExecutor::Data&& executor_data) noexcept {
  return internal::AbstractPromise::Create(
      task_runner, from_here,
      std::make_unique<AbstractPromise::AdjacencyList>(prerequsite),
      RejectPolicy::kMustCatchRejection,
      internal::DependentList::ConstructUnresolved(), std::move(executor_data));
}

scoped_refptr<AbstractPromise> ConstructManualPromiseResolverPromise(
    const Location& from_here,
    RejectPolicy reject_policy,
    bool can_resolve,
    bool can_reject) {
  return internal::AbstractPromise::CreateNoPrerequisitePromise(
      from_here, reject_policy, internal::DependentList::ConstructUnresolved(),
      internal::PromiseExecutor::Data(
          in_place_type_t<internal::NoOpPromiseExecutor>(), can_resolve,
          can_reject));
}

}  // namespace internal
}  // namespace base
