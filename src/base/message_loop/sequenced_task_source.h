// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MESSAGE_LOOP_SEQUENCED_TASK_SOURCE_H_
#define BASE_MESSAGE_LOOP_SEQUENCED_TASK_SOURCE_H_

#include "base/callback.h"
#include "base/pending_task.h"
#include "base/time/time.h"

namespace base {

// A source of tasks to be executed sequentially. Unless specified otherwise,
// methods below are not thread-safe (must be called from the executing
// sequence).
// TODO(scheduler-dev): Coalesce with
// base::sequence_manager::SequencedTaskSource.
class SequencedTaskSource {
 public:
  class Observer {
   public:
    virtual ~Observer() = default;

    // Notifies this Observer that |task| is about to be enqueued in the
    // SequencedTaskSource it observes.
    // WillQueueTask() may be invoked from any thread.
    virtual void WillQueueTask(PendingTask* task) = 0;

    // Notifies this Observer that a task was enqueued in the
    // SequencedTaskSource it observes. |was_empty| is true if the task source
    // was empty (i.e. |!HasTasks()|) before this task was posted.
    // DidQueueTask() may be invoked from any thread.
    virtual void DidQueueTask(bool was_empty) = 0;
  };

  virtual ~SequencedTaskSource() = default;

  // Take a next task to run from a sequence. Must only be called if
  // HasTasks() returns true.
  virtual PendingTask TakeTask() = 0;

  // Returns true if this SequencedTaskSource will return a task from the next
  // TakeTask() call.
  virtual bool HasTasks() = 0;

  // Injects |task| at the end of this SequencedTaskSource (such that it will be
  // the last task returned by TakeTask() if no other task are posted after this
  // point). TODO(gab): This is only required to support clearing tasks on
  // shutdown, maybe leaking tasks on shutdown is a better alternative.
  virtual void InjectTask(OnceClosure task) = 0;
};

}  // namespace base

#endif  // BASE_MESSAGE_LOOP_SEQUENCED_TASK_SOURCE_H_
