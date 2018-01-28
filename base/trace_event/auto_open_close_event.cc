// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/auto_open_close_event.h"

#include "base/macros.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"

namespace base {
namespace trace_event {

AutoOpenCloseEvent::AutoOpenCloseEvent(AutoOpenCloseEvent::Type type,
  const char* category, const char* event_name):
  category_(category),
  event_name_(event_name),
  weak_factory_(this) {
  base::trace_event::TraceLog::GetInstance()->AddAsyncEnabledStateObserver(
      weak_factory_.GetWeakPtr());
}

AutoOpenCloseEvent::~AutoOpenCloseEvent() {
  DCHECK(thread_checker_.CalledOnValidThread());
  base::trace_event::TraceLog::GetInstance()->RemoveAsyncEnabledStateObserver(
      this);
}

void AutoOpenCloseEvent::Begin() {
  DCHECK(thread_checker_.CalledOnValidThread());
  start_time_ = base::TimeTicks::Now();
  TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP0(
      category_, event_name_, static_cast<void*>(this), start_time_);
}

void AutoOpenCloseEvent::End() {
  DCHECK(thread_checker_.CalledOnValidThread());
  TRACE_EVENT_ASYNC_END0(category_, event_name_, static_cast<void*>(this));
  start_time_ = base::TimeTicks();
}

void AutoOpenCloseEvent::OnTraceLogEnabled() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (start_time_.ToInternalValue() != 0)
    TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP0(
        category_, event_name_, static_cast<void*>(this), start_time_);
}

void AutoOpenCloseEvent::OnTraceLogDisabled() {}

}  // namespace trace_event
}  // namespace base