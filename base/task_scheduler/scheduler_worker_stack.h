// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SCHEDULER_SCHEDULER_WORKER_STACK_H_
#define BASE_TASK_SCHEDULER_SCHEDULER_WORKER_STACK_H_

#include <stddef.h>

#include <vector>

#include "base/base_export.h"
#include "base/macros.h"

namespace base {
namespace internal {

class SchedulerWorker;

// A stack of SchedulerWorkers. Supports removal of arbitrary SchedulerWorkers.
// DCHECKs when a SchedulerWorker is inserted multiple times. SchedulerWorkers
// are not owned by the stack. Push() is amortized O(1). Pop(), Peek(), Size()
// and Empty() are O(1). Contains() and Remove() are O(n).
// This class is NOT thread-safe.
class BASE_EXPORT SchedulerWorkerStack {
 public:
  SchedulerWorkerStack();
  ~SchedulerWorkerStack();

  // Inserts |worker| at the top of the stack. |worker| must not already be on
  // the stack.
  void Push(SchedulerWorker* worker);

  // Removes the top SchedulerWorker from the stack and returns it.
  // Returns nullptr if the stack is empty.
  SchedulerWorker* Pop();

  // Returns the top SchedulerWorker from the stack, nullptr if empty.
  SchedulerWorker* Peek() const;

  // Returns true if |worker| is already on the stack.
  bool Contains(const SchedulerWorker* worker) const;

  // Removes |worker| from the stack.
  void Remove(const SchedulerWorker* worker);

  // Returns the number of SchedulerWorkers on the stack.
  size_t Size() const { return stack_.size(); }

  // Returns true if the stack is empty.
  bool IsEmpty() const { return stack_.empty(); }

 private:
  std::vector<SchedulerWorker*> stack_;

  DISALLOW_COPY_AND_ASSIGN(SchedulerWorkerStack);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_SCHEDULER_SCHEDULER_WORKER_STACK_H_
