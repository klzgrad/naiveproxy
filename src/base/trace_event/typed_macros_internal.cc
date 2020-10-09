// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/typed_macros_internal.h"

#include "base/optional.h"
#include "base/time/time.h"
#include "base/trace_event/thread_instruction_count.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/typed_macros.h"

namespace {

base::ThreadTicks ThreadNow() {
  return base::ThreadTicks::IsSupported()
             ? base::subtle::ThreadTicksNowIgnoringOverride()
             : base::ThreadTicks();
}

base::trace_event::ThreadInstructionCount ThreadInstructionNow() {
  return base::trace_event::ThreadInstructionCount::IsSupported()
             ? base::trace_event::ThreadInstructionCount::Now()
             : base::trace_event::ThreadInstructionCount();
}

base::trace_event::PrepareTrackEventFunction g_typed_event_callback = nullptr;

}  // namespace

namespace base {
namespace trace_event {

void EnableTypedTraceEvents(PrepareTrackEventFunction typed_event_callback) {
  g_typed_event_callback = typed_event_callback;
}

void ResetTypedTraceEventsForTesting() {
  g_typed_event_callback = nullptr;
}

TrackEventHandle::CompletionListener::~CompletionListener() = default;

}  // namespace trace_event
}  // namespace base

namespace trace_event_internal {

base::trace_event::TrackEventHandle CreateTrackEvent(
    char phase,
    const unsigned char* category_group_enabled,
    const char* name,
    unsigned int flags,
    base::TimeTicks ts,
    bool explicit_track) {
  DCHECK(phase == TRACE_EVENT_PHASE_BEGIN || phase == TRACE_EVENT_PHASE_END ||
         phase == TRACE_EVENT_PHASE_INSTANT);
  DCHECK(category_group_enabled);

  if (!g_typed_event_callback)
    return base::trace_event::TrackEventHandle();

  const int thread_id = static_cast<int>(base::PlatformThread::CurrentId());
  auto* trace_log = base::trace_event::TraceLog::GetInstance();
  DCHECK(trace_log);
  if (!trace_log->ShouldAddAfterUpdatingState(phase, category_group_enabled,
                                              name, trace_event_internal::kNoId,
                                              thread_id, nullptr)) {
    return base::trace_event::TrackEventHandle();
  }

  if (ts.is_null()) {
    ts = TRACE_TIME_TICKS_NOW();
  } else {
    flags |= TRACE_EVENT_FLAG_EXPLICIT_TIMESTAMP;
  }

  // Only emit thread time / instruction count for events on the default track
  // without explicit timestamp.
  base::ThreadTicks thread_now;
  base::trace_event::ThreadInstructionCount thread_instruction_now;
  if ((flags & TRACE_EVENT_FLAG_EXPLICIT_TIMESTAMP) == 0 && !explicit_track) {
    thread_now = ThreadNow();
    thread_instruction_now = ThreadInstructionNow();
  }

  base::trace_event::TraceEvent event(
      thread_id, ts, thread_now, thread_instruction_now, phase,
      category_group_enabled, name, trace_event_internal::kGlobalScope,
      trace_event_internal::kNoId, trace_event_internal::kNoId, nullptr, flags);

  return g_typed_event_callback(&event);
}

}  // namespace trace_event_internal
