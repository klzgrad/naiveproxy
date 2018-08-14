// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_WORKER_POOL_H_
#define UTIL_WORKER_POOL_H_

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "base/logging.h"
#include "base/macros.h"
#include "util/task.h"

class WorkerPool {
 public:
  WorkerPool();
  WorkerPool(size_t thread_count);
  ~WorkerPool();

  void PostTask(Task work);

 private:
  void Worker();

  std::vector<std::thread> threads_;
  std::queue<base::OnceClosure> task_queue_;
  std::mutex queue_mutex_;
  std::condition_variable_any pool_notifier_;
  bool should_stop_processing_;

  DISALLOW_COPY_AND_ASSIGN(WorkerPool);
};

#endif  // UTIL_WORKER_POOL_H_
