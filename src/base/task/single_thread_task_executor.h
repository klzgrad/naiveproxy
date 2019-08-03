// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SINGLE_THREAD_TASK_EXECUTOR_H_
#define BASE_TASK_SINGLE_THREAD_TASK_EXECUTOR_H_

#include "base/base_export.h"
#include "base/message_loop/message_loop.h"

namespace base {

namespace sequence_manager {
class SequenceManager;
class TaskQueue;
}  // namespace sequence_manager

// A simple single thread TaskExecutor intended for non-test usage. Tests should
// generally use ScopedTaskEnvironment or TestBrowserThreadBundle instead.
// TODO(alexclarke): Inherit from TaskExecutor to support base::Here().
class BASE_EXPORT SingleThreadTaskExecutor {
 public:
  explicit SingleThreadTaskExecutor(
      MessagePump::Type type = MessagePump::Type::DEFAULT);

  // Shuts down the SingleThreadTaskExecutor, after this no tasks can be
  // executed and the base::TaskExecutor APIs are non-functional but won't crash
  // if called.
  ~SingleThreadTaskExecutor();

  scoped_refptr<SingleThreadTaskRunner> task_runner() const;

  MessagePump::Type type() const { return type_; }

 private:
  std::unique_ptr<sequence_manager::SequenceManager> sequence_manager_;
  scoped_refptr<sequence_manager::TaskQueue> default_task_queue_;
  MessagePump::Type type_;

  DISALLOW_COPY_AND_ASSIGN(SingleThreadTaskExecutor);
};

}  // namespace base

#endif  // BASE_TASK_SINGLE_THREAD_TASK_EXECUTOR_H_
