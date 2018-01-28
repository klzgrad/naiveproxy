// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/scheduler_worker_stack.h"

#include <algorithm>

#include "base/logging.h"

namespace base {
namespace internal {

SchedulerWorkerStack::SchedulerWorkerStack() = default;

SchedulerWorkerStack::~SchedulerWorkerStack() = default;

void SchedulerWorkerStack::Push(SchedulerWorker* worker) {
  DCHECK(!Contains(worker)) << "SchedulerWorker already on stack";
  stack_.push_back(worker);
}

SchedulerWorker* SchedulerWorkerStack::Pop() {
  if (IsEmpty())
    return nullptr;
  SchedulerWorker* const worker = stack_.back();
  stack_.pop_back();
  return worker;
}

SchedulerWorker* SchedulerWorkerStack::Peek() const {
  if (IsEmpty())
    return nullptr;
  return stack_.back();
}

bool SchedulerWorkerStack::Contains(const SchedulerWorker* worker) const {
  return std::find(stack_.begin(), stack_.end(), worker) != stack_.end();
}

void SchedulerWorkerStack::Remove(const SchedulerWorker* worker) {
  auto it = std::find(stack_.begin(), stack_.end(), worker);
  if (it != stack_.end())
    stack_.erase(it);
}

}  // namespace internal
}  // namespace base
