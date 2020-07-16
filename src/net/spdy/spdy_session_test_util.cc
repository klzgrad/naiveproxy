// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_session_test_util.h"

#include "base/location.h"
#include "base/message_loop/message_loop_current.h"
#include "base/strings/string_util.h"

namespace net {

SpdySessionTestTaskObserver::SpdySessionTestTaskObserver(
    const std::string& file_name,
    const std::string& function_name)
    : executed_count_(0), file_name_(file_name), function_name_(function_name) {
  base::MessageLoopCurrent::Get()->AddTaskObserver(this);
}

SpdySessionTestTaskObserver::~SpdySessionTestTaskObserver() {
  base::MessageLoopCurrent::Get()->RemoveTaskObserver(this);
}

void SpdySessionTestTaskObserver::WillProcessTask(
    const base::PendingTask& pending_task,
    bool was_blocked_or_low_priority) {}

void SpdySessionTestTaskObserver::DidProcessTask(
    const base::PendingTask& pending_task) {
  if (base::EndsWith(pending_task.posted_from.file_name(), file_name_,
                     base::CompareCase::SENSITIVE) &&
      base::EndsWith(pending_task.posted_from.function_name(), function_name_,
                     base::CompareCase::SENSITIVE)) {
    ++executed_count_;
  }
}

}  // namespace net
