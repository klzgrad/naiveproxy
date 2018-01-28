// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/serial_worker.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/thread_task_runner_handle.h"

namespace net {

SerialWorker::SerialWorker() : state_(IDLE) {}

SerialWorker::~SerialWorker() = default;

void SerialWorker::WorkNow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  switch (state_) {
    case IDLE:
      base::PostTaskWithTraitsAndReply(
          FROM_HERE,
          {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
          base::BindOnce(&SerialWorker::DoWork, this),
          base::BindOnce(&SerialWorker::OnWorkJobFinished, this));
      state_ = WORKING;
      return;
    case WORKING:
      // Remember to re-read after |DoRead| finishes.
      state_ = PENDING;
      return;
    case CANCELLED:
    case PENDING:
      return;
    default:
      NOTREACHED() << "Unexpected state " << state_;
  }
}

void SerialWorker::Cancel() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  state_ = CANCELLED;
}

void SerialWorker::OnWorkJobFinished() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  switch (state_) {
    case CANCELLED:
      return;
    case WORKING:
      state_ = IDLE;
      this->OnWorkFinished();
      return;
    case PENDING:
      state_ = IDLE;
      WorkNow();
      return;
    default:
      NOTREACHED() << "Unexpected state " << state_;
  }
}

}  // namespace net
