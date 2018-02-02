// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/sequenced_task_runner_handle.h"

#include <utility>

#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/thread_local.h"
#include "base/threading/thread_task_runner_handle.h"

namespace base {

namespace {

LazyInstance<ThreadLocalPointer<SequencedTaskRunnerHandle>>::Leaky
    sequenced_task_runner_tls = LAZY_INSTANCE_INITIALIZER;

}  // namespace

// static
scoped_refptr<SequencedTaskRunner> SequencedTaskRunnerHandle::Get() {
  // Return the registered SequencedTaskRunner, if any.
  const SequencedTaskRunnerHandle* handle =
      sequenced_task_runner_tls.Pointer()->Get();
  if (handle) {
    // Various modes of setting SequencedTaskRunnerHandle don't combine.
    DCHECK(!SequencedWorkerPool::GetSequenceTokenForCurrentThread().IsValid());

    return handle->task_runner_;
  }

  // If we are on a worker thread for a SequencedWorkerPool that is running a
  // sequenced task, return a SequencedTaskRunner for it.
  scoped_refptr<SequencedWorkerPool> pool =
      SequencedWorkerPool::GetWorkerPoolForCurrentThread();
  if (pool) {
    SequencedWorkerPool::SequenceToken sequence_token =
        SequencedWorkerPool::GetSequenceTokenForCurrentThread();
    DCHECK(sequence_token.IsValid());
    scoped_refptr<SequencedTaskRunner> sequenced_task_runner(
        pool->GetSequencedTaskRunner(sequence_token));
    DCHECK(sequenced_task_runner->RunsTasksInCurrentSequence());
    return sequenced_task_runner;
  }

  // Note if you hit this: the problem is the lack of a sequenced context. The
  // ThreadTaskRunnerHandle is just the last attempt at finding such a context.
  CHECK(ThreadTaskRunnerHandle::IsSet())
      << "Error: This caller requires a sequenced context (i.e. the "
         "current task needs to run from a SequencedTaskRunner).";
  return ThreadTaskRunnerHandle::Get();
}

// static
bool SequencedTaskRunnerHandle::IsSet() {
  return sequenced_task_runner_tls.Pointer()->Get() ||
         SequencedWorkerPool::GetSequenceTokenForCurrentThread().IsValid() ||
         ThreadTaskRunnerHandle::IsSet();
}

SequencedTaskRunnerHandle::SequencedTaskRunnerHandle(
    scoped_refptr<SequencedTaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!SequencedTaskRunnerHandle::IsSet());
  sequenced_task_runner_tls.Pointer()->Set(this);
}

SequencedTaskRunnerHandle::~SequencedTaskRunnerHandle() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(sequenced_task_runner_tls.Pointer()->Get(), this);
  sequenced_task_runner_tls.Pointer()->Set(nullptr);
}

}  // namespace base
