// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop_task_runner.h"

#include <utility>

#include "base/location.h"
#include "base/logging.h"
#include "base/message_loop/incoming_task_queue.h"

namespace base {
namespace internal {

MessageLoopTaskRunner::MessageLoopTaskRunner(
    scoped_refptr<IncomingTaskQueue> incoming_queue)
    : incoming_queue_(incoming_queue), valid_thread_id_(kInvalidThreadId) {
}

void MessageLoopTaskRunner::BindToCurrentThread() {
  AutoLock lock(valid_thread_id_lock_);
  DCHECK_EQ(kInvalidThreadId, valid_thread_id_);
  valid_thread_id_ = PlatformThread::CurrentId();
}

bool MessageLoopTaskRunner::PostDelayedTask(const Location& from_here,
                                            OnceClosure task,
                                            base::TimeDelta delay) {
  DCHECK(!task.is_null()) << from_here.ToString();
  return incoming_queue_->AddToIncomingQueue(from_here, std::move(task), delay,
                                             Nestable::kNestable);
}

bool MessageLoopTaskRunner::PostNonNestableDelayedTask(
    const Location& from_here,
    OnceClosure task,
    base::TimeDelta delay) {
  DCHECK(!task.is_null()) << from_here.ToString();
  return incoming_queue_->AddToIncomingQueue(from_here, std::move(task), delay,
                                             Nestable::kNonNestable);
}

bool MessageLoopTaskRunner::RunsTasksInCurrentSequence() const {
  AutoLock lock(valid_thread_id_lock_);
  return valid_thread_id_ == PlatformThread::CurrentId();
}

MessageLoopTaskRunner::~MessageLoopTaskRunner() {
}

}  // namespace internal

}  // namespace base
