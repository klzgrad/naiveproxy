// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_DEBUG_TASK_ANNOTATOR_H_
#define BASE_DEBUG_TASK_ANNOTATOR_H_

#include <stdint.h>

#include "base/base_export.h"
#include "base/macros.h"

namespace base {
struct PendingTask;
namespace debug {

// Implements common debug annotations for posted tasks. This includes data
// such as task origins, queueing durations and memory usage.
class BASE_EXPORT TaskAnnotator {
 public:
  class ObserverForTesting {
   public:
    // Invoked just before RunTask() in the scope in which the task is about to
    // be executed.
    virtual void BeforeRunTask(const PendingTask* pending_task) = 0;
  };

  TaskAnnotator();
  ~TaskAnnotator();

  // Called to indicate that a task is about to be queued to run in the future,
  // giving one last chance for this TaskAnnotator to add metadata to
  // |pending_task| before it is moved into the queue. |queue_function| is used
  // as the trace flow event name. |queue_function| can be null if the caller
  // doesn't want trace flow events logged to toplevel.flow.
  void WillQueueTask(const char* queue_function, PendingTask* pending_task);

  // Run a previously queued task. |queue_function| should match what was
  // passed into |DidQueueTask| for this task.
  void RunTask(const char* queue_function, PendingTask* pending_task);

  // Creates a process-wide unique ID to represent this task in trace events.
  // This will be mangled with a Process ID hash to reduce the likelyhood of
  // colliding with TaskAnnotator pointers on other processes. Callers may use
  // this when generating their own flow events (i.e. when passing
  // |queue_function == nullptr| in above methods).
  uint64_t GetTaskTraceID(const PendingTask& task) const;

 private:
  friend class TaskAnnotatorBacktraceIntegrationTest;

  // Registers an ObserverForTesting that will be invoked by all TaskAnnotators'
  // RunTask(). This registration and the implementation of BeforeRunTask() are
  // responsible to ensure thread-safety.
  static void RegisterObserverForTesting(ObserverForTesting* observer);
  static void ClearObserverForTesting();

  DISALLOW_COPY_AND_ASSIGN(TaskAnnotator);
};

}  // namespace debug
}  // namespace base

#endif  // BASE_DEBUG_TASK_ANNOTATOR_H_
