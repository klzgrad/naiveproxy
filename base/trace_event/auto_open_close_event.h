// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_AUTO_OPEN_CLOSE_EVENT_H_
#define BASE_AUTO_OPEN_CLOSE_EVENT_H_

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"

namespace base {
namespace trace_event {

// Class for tracing events that support "auto-opening" and "auto-closing".
// "auto-opening" = if the trace event is started (call Begin() before
// tracing is started,the trace event will be opened, with the start time
// being the time that the trace event was actually started.
// "auto-closing" = if the trace event is started but not ended by the time
// tracing ends, then the trace event will be automatically closed at the
// end of tracing.
class BASE_EXPORT AutoOpenCloseEvent
    : public TraceLog::AsyncEnabledStateObserver {
 public:
  enum Type {
    ASYNC
  };

  // As in the rest of the tracing macros, the const char* arguments here
  // must be pointers to indefinitely lived strings (e.g. hard-coded string
  // literals are okay, but not strings created by c_str())
  AutoOpenCloseEvent(Type type, const char* category, const char* event_name);
  ~AutoOpenCloseEvent() override;

  void Begin();
  void End();

  // AsyncEnabledStateObserver implementation
  void OnTraceLogEnabled() override;
  void OnTraceLogDisabled() override;

 private:
  const char* const category_;
  const char* const event_name_;
  base::TimeTicks start_time_;
  base::ThreadChecker thread_checker_;
  WeakPtrFactory<AutoOpenCloseEvent> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(AutoOpenCloseEvent);
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_AUTO_OPEN_CLOSE_EVENT_H_