// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_TASK_TASK_RUNNER_H_
#define NET_BASE_TASK_TASK_RUNNER_H_

#include "base/memory/scoped_refptr.h"
#include "base/task/single_thread_task_runner.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"

namespace net {

// Retrieves a task runner suitable for the given `priority`.
//
// This function allows different parts of the //net stack to obtain task
// runners that are integrated with the network service's scheduling mechanism
// (or other embedder's scheduling). For `RequestPriority::HIGHEST`, this may
// return a special high-priority task runner if one has been configured (e.g.,
// by the NetworkServiceScheduler). For other priorities, or if no special
// runner is configured, it typically returns the current thread's default task
// runner.
NET_EXPORT const scoped_refptr<base::SingleThreadTaskRunner>& GetTaskRunner(
    RequestPriority priority);

namespace internal {

// A struct holding global task runner instances that can be set by an
// embedder (like the network service scheduler). This allows `GetTaskRunner`
// to return specialized runners.
struct NET_EXPORT TaskRunnerGlobals {
  TaskRunnerGlobals();
  ~TaskRunnerGlobals();

  // Task runner specifically for `net::RequestPriority::HIGHEST` tasks.
  // This is set by the embedder (e.g., NetworkServiceScheduler).
  scoped_refptr<base::SingleThreadTaskRunner> high_priority_task_runner;
};

NET_EXPORT TaskRunnerGlobals& GetTaskRunnerGlobals();

}  // namespace internal

}  // namespace net

#endif  // NET_BASE_TASK_TASK_RUNNER_H_
