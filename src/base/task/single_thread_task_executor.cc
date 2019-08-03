// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/single_thread_task_executor.h"

#include "base/message_loop/message_pump.h"
#include "base/task/sequence_manager/sequence_manager.h"

namespace base {

SingleThreadTaskExecutor::SingleThreadTaskExecutor(MessagePump::Type type)
    : sequence_manager_(
          sequence_manager::CreateSequenceManagerOnCurrentThreadWithPump(
              MessagePump::Create(type),
              sequence_manager::SequenceManager::Settings::Builder()
                  .SetMessagePumpType(type)
                  .Build())),
      default_task_queue_(sequence_manager_->CreateTaskQueue(
          sequence_manager::TaskQueue::Spec("default_tq"))),
      type_(type) {
  sequence_manager_->SetDefaultTaskRunner(default_task_queue_->task_runner());
}

SingleThreadTaskExecutor::~SingleThreadTaskExecutor() = default;

scoped_refptr<SingleThreadTaskRunner> SingleThreadTaskExecutor::task_runner()
    const {
  return default_task_queue_->task_runner();
}

}  // namespace base
