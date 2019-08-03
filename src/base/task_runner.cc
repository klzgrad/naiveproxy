// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_runner.h"

#include <utility>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/task/promise/abstract_promise.h"
#include "base/threading/post_task_and_reply_impl.h"

namespace base {

namespace {

// TODO(akalin): There's only one other implementation of
// PostTaskAndReplyImpl in post_task.cc.  Investigate whether it'll be
// possible to merge the two.
class PostTaskAndReplyTaskRunner : public internal::PostTaskAndReplyImpl {
 public:
  explicit PostTaskAndReplyTaskRunner(TaskRunner* destination);

 private:
  bool PostTask(const Location& from_here, OnceClosure task) override;

  // Non-owning.
  TaskRunner* destination_;
};

PostTaskAndReplyTaskRunner::PostTaskAndReplyTaskRunner(
    TaskRunner* destination) : destination_(destination) {
  DCHECK(destination_);
}

bool PostTaskAndReplyTaskRunner::PostTask(const Location& from_here,
                                          OnceClosure task) {
  return destination_->PostTask(from_here, std::move(task));
}

// TODO(alexclarke): Remove this when TaskRunner::PostPromiseInternal becomes
// pure virtual.
class PromiseHolder {
 public:
  explicit PromiseHolder(scoped_refptr<internal::AbstractPromise> promise)
      : promise_(std::move(promise)) {}

  ~PromiseHolder() {
    // Detect if the promise was not executed and if so cancel to ensure memory
    // is released.
    if (promise_)
      promise_->OnCanceled();
  }

  PromiseHolder(PromiseHolder&& other) : promise_(std::move(other.promise_)) {}

  scoped_refptr<internal::AbstractPromise> Unwrap() const {
    return std::move(promise_);
  }

 private:
  mutable scoped_refptr<internal::AbstractPromise> promise_;
};

}  // namespace

template <>
struct BindUnwrapTraits<PromiseHolder> {
  static scoped_refptr<internal::AbstractPromise> Unwrap(
      const PromiseHolder& o) {
    return o.Unwrap();
  }
};

bool TaskRunner::PostTask(const Location& from_here, OnceClosure task) {
  return PostDelayedTask(from_here, std::move(task), base::TimeDelta());
}

bool TaskRunner::PostTaskAndReply(const Location& from_here,
                                  OnceClosure task,
                                  OnceClosure reply) {
  return PostTaskAndReplyTaskRunner(this).PostTaskAndReply(
      from_here, std::move(task), std::move(reply));
}

bool TaskRunner::PostPromiseInternal(
    const scoped_refptr<internal::AbstractPromise>& promise,
    base::TimeDelta delay) {
  return PostDelayedTask(
      promise->from_here(),
      BindOnce(&internal::AbstractPromise::Execute, PromiseHolder(promise)),
      delay);
}

TaskRunner::TaskRunner() = default;

TaskRunner::~TaskRunner() = default;

void TaskRunner::OnDestruct() const {
  delete this;
}

void TaskRunnerTraits::Destruct(const TaskRunner* task_runner) {
  task_runner->OnDestruct();
}

}  // namespace base
