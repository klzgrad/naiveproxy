// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_THREADING_THREAD_TASK_RUNNER_HANDLE_H_
#define BASE_THREADING_THREAD_TASK_RUNNER_HANDLE_H_

#include "base/base_export.h"
#include "base/callback_helpers.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/single_thread_task_runner.h"

namespace base {

// ThreadTaskRunnerHandle stores a reference to a thread's TaskRunner
// in thread-local storage.  Callers can then retrieve the TaskRunner
// for the current thread by calling ThreadTaskRunnerHandle::Get().
// At most one TaskRunner may be bound to each thread at a time.
// Prefer SequencedTaskRunnerHandle to this unless thread affinity is required.
class BASE_EXPORT ThreadTaskRunnerHandle {
 public:
  // Gets the SingleThreadTaskRunner for the current thread.
  static scoped_refptr<SingleThreadTaskRunner> Get();

  // Returns true if the SingleThreadTaskRunner is already created for
  // the current thread.
  static bool IsSet();

  // Overrides ThreadTaskRunnerHandle::Get()'s |task_runner_| to point at
  // |overriding_task_runner| until the returned ScopedClosureRunner goes out of
  // scope (instantiates a ThreadTaskRunnerHandle for that scope if |!IsSet()|).
  // Nested overrides are allowed but callers must ensure the
  // ScopedClosureRunners expire in LIFO (stack) order. Note: nesting
  // ThreadTaskRunnerHandles isn't generally desired but it's useful in unit
  // tests where multiple task runners can share the main thread for simplicity
  // and determinism.
  static ScopedClosureRunner OverrideForTesting(
      scoped_refptr<SingleThreadTaskRunner> overriding_task_runner)
      WARN_UNUSED_RESULT;

  // Binds |task_runner| to the current thread. |task_runner| must belong
  // to the current thread for this to succeed.
  explicit ThreadTaskRunnerHandle(
      scoped_refptr<SingleThreadTaskRunner> task_runner);
  ~ThreadTaskRunnerHandle();

 private:
  scoped_refptr<SingleThreadTaskRunner> task_runner_;

  DISALLOW_COPY_AND_ASSIGN(ThreadTaskRunnerHandle);
};

}  // namespace base

#endif  // BASE_THREADING_THREAD_TASK_RUNNER_HANDLE_H_
