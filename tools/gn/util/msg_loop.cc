// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util/msg_loop.h"

#include "base/logging.h"

namespace {

thread_local MsgLoop* g_current;
}

MsgLoop::MsgLoop() {
  DCHECK(g_current == nullptr);
  g_current = this;
}

MsgLoop::~MsgLoop() {
  DCHECK(g_current == this);
  g_current = nullptr;
}

void MsgLoop::Run() {
  while (!should_quit_) {
    Task task;
    {
      std::unique_lock<std::mutex> queue_lock(queue_mutex_);
      notifier_.wait(queue_lock, [this]() {
        return (!task_queue_.empty()) || should_quit_;
      });

      if (should_quit_)
        return;

      task = std::move(task_queue_.front());
      task_queue_.pop();
    }

    std::move(task).Run();
  }
}

void MsgLoop::PostQuit() {
  PostTask(
      base::BindOnce([](MsgLoop* self) { self->should_quit_ = true; }, this));
}

void MsgLoop::PostTask(Task work) {
  {
    std::unique_lock<std::mutex> queue_lock(queue_mutex_);
    task_queue_.emplace(std::move(work));
  }

  notifier_.notify_one();
}

void MsgLoop::RunUntilIdleForTesting() {
  for (bool done = false; !done;) {
    Task task;
    {
      std::unique_lock<std::mutex> queue_lock(queue_mutex_);
      task = std::move(task_queue_.front());
      task_queue_.pop();

      if (task_queue_.empty())
        done = true;
    }

    std::move(task).Run();
  }
}

MsgLoop* MsgLoop::Current() {
  return g_current;
}
