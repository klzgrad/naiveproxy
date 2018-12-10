// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_RUN_LOOP_H_
#define UTIL_RUN_LOOP_H_

#include "base/macros.h"
#include "util/task.h"

#include <condition_variable>
#include <mutex>
#include <queue>

class MsgLoop {
 public:
  MsgLoop();
  ~MsgLoop();

  // Blocks until PostQuit() is called, processing work items posted via
  void Run();

  // Schedules Run() to exit, but will not happen until other outstanding tasks
  // complete. Can be called from any thread.
  void PostQuit();

  // Posts a work item to this queue. All items will be run on the thread from
  // which Run() was called. Can be called from any thread.
  void PostTask(Task task);

  // Run()s until the queue is empty. Should only be used (carefully) in tests.
  void RunUntilIdleForTesting();

  // Gets the MsgLoop for the thread from which it's called, or nullptr if
  // there's no MsgLoop for the current thread.
  static MsgLoop* Current();

 private:
  std::mutex queue_mutex_;
  std::queue<Task> task_queue_;
  std::condition_variable notifier_;
  bool should_quit_ = false;

  DISALLOW_COPY_AND_ASSIGN(MsgLoop);
};

#endif  // UTIL_RUN_LOOP_H_
