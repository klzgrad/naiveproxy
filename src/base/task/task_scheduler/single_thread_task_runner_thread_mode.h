// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_SCHEDULER_SINGLE_THREAD_TASK_RUNNER_THREAD_MODE_H_
#define BASE_TASK_TASK_SCHEDULER_SINGLE_THREAD_TASK_RUNNER_THREAD_MODE_H_

namespace base {

enum class SingleThreadTaskRunnerThreadMode {
  // Allow the SingleThreadTaskRunner's thread to be shared with others,
  // allowing for efficient use of thread resources when this
  // SingleThreadTaskRunner is idle. This is the default mode and is
  // recommended for most code.
  SHARED,
  // Dedicate a single thread for this SingleThreadTaskRunner. No other tasks
  // from any other source will run on the thread backing the
  // SingleThreadTaskRunner. Use sparingly as this reserves an entire thread for
  // this SingleThreadTaskRunner.
  DEDICATED,
};

}  // namespace base

#endif  // BASE_TASK_TASK_SCHEDULER_SINGLE_THREAD_TASK_RUNNER_THREAD_MODE_H_
