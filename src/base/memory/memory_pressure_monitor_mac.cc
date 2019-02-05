// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/memory_pressure_monitor_mac.h"

#include <CoreFoundation/CoreFoundation.h>

#include <dlfcn.h>
#include <stddef.h>
#include <sys/sysctl.h>

#include <cmath>

#include "base/bind.h"
#include "base/logging.h"
#include "base/mac/mac_util.h"

// Redeclare for partial 10.9 availability.
DISPATCH_EXPORT const struct dispatch_source_type_s
    _dispatch_source_type_memorypressure;

namespace {
static const int kUMATickSize = 5;
}  // namespace

namespace base {
namespace mac {

MemoryPressureListener::MemoryPressureLevel
MemoryPressureMonitor::MemoryPressureLevelForMacMemoryPressureLevel(
    int mac_memory_pressure_level) {
  switch (mac_memory_pressure_level) {
    case DISPATCH_MEMORYPRESSURE_NORMAL:
      return MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE;
    case DISPATCH_MEMORYPRESSURE_WARN:
      return MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE;
    case DISPATCH_MEMORYPRESSURE_CRITICAL:
      return MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL;
  }
  return MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE;
}

void MemoryPressureMonitor::OnRunLoopExit(CFRunLoopObserverRef observer,
                                          CFRunLoopActivity activity,
                                          void* info) {
  MemoryPressureMonitor* self = static_cast<MemoryPressureMonitor*>(info);
  self->UpdatePressureLevelOnRunLoopExit();
}

MemoryPressureMonitor::MemoryPressureMonitor()
    : memory_level_event_source_(dispatch_source_create(
          DISPATCH_SOURCE_TYPE_MEMORYPRESSURE,
          0,
          DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL |
              DISPATCH_MEMORYPRESSURE_NORMAL,
          dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0))),
      dispatch_callback_(
          base::Bind(&MemoryPressureListener::NotifyMemoryPressure)),
      last_statistic_report_time_(CFAbsoluteTimeGetCurrent()),
      last_pressure_level_(MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE),
      subtick_seconds_(0) {
  // Attach an event handler to the memory pressure event source.
  if (memory_level_event_source_.get()) {
    dispatch_source_set_event_handler(memory_level_event_source_, ^{
      OnMemoryPressureChanged(memory_level_event_source_.get(),
                              dispatch_callback_);
    });

    // Start monitoring the event source.
    dispatch_resume(memory_level_event_source_);
  }

  // Create a CFRunLoopObserver to check the memory pressure at the end of
  // every pass through the event loop (modulo kUMATickSize).
  CFRunLoopObserverContext observer_context = {0, this, NULL, NULL, NULL};

  exit_observer_.reset(
      CFRunLoopObserverCreate(kCFAllocatorDefault, kCFRunLoopExit, true, 0,
                              OnRunLoopExit, &observer_context));

  CFRunLoopRef run_loop = CFRunLoopGetCurrent();
  CFRunLoopAddObserver(run_loop, exit_observer_, kCFRunLoopCommonModes);
  CFRunLoopAddObserver(run_loop, exit_observer_,
                       kMessageLoopExclusiveRunLoopMode);
}

MemoryPressureMonitor::~MemoryPressureMonitor() {
  // Detach from the run loop.
  CFRunLoopRef run_loop = CFRunLoopGetCurrent();
  CFRunLoopRemoveObserver(run_loop, exit_observer_, kCFRunLoopCommonModes);
  CFRunLoopRemoveObserver(run_loop, exit_observer_,
                          kMessageLoopExclusiveRunLoopMode);

  // Remove the memory pressure event source.
  if (memory_level_event_source_.get()) {
    dispatch_source_cancel(memory_level_event_source_);
  }
}

int MemoryPressureMonitor::GetMacMemoryPressureLevel() {
  // Get the raw memory pressure level from macOS.
  int mac_memory_pressure_level;
  size_t length = sizeof(int);
  sysctlbyname("kern.memorystatus_vm_pressure_level",
               &mac_memory_pressure_level, &length, nullptr, 0);

  return mac_memory_pressure_level;
}

void MemoryPressureMonitor::UpdatePressureLevel() {
  // Get the current macOS pressure level and convert to the corresponding
  // Chrome pressure level.
  int mac_memory_pressure_level = GetMacMemoryPressureLevel();
  MemoryPressureListener::MemoryPressureLevel new_pressure_level =
      MemoryPressureLevelForMacMemoryPressureLevel(mac_memory_pressure_level);

  // Compute the number of "ticks" spent at |last_pressure_level_| (since the
  // last report sent to UMA).
  CFTimeInterval now = CFAbsoluteTimeGetCurrent();
  CFTimeInterval time_since_last_report = now - last_statistic_report_time_;
  last_statistic_report_time_ = now;

  double accumulated_time = time_since_last_report + subtick_seconds_;
  int ticks_to_report = static_cast<int>(accumulated_time / kUMATickSize);
  // Save for later the seconds that didn't make it into a full tick.
  subtick_seconds_ = std::fmod(accumulated_time, kUMATickSize);

  // Round the tick count up on a pressure level change to ensure we capture it.
  bool pressure_level_changed = (new_pressure_level != last_pressure_level_);
  if (pressure_level_changed && ticks_to_report < 1) {
    ticks_to_report = 1;
    subtick_seconds_ = 0;
  }

  // Send elapsed ticks to UMA.
  if (ticks_to_report >= 1) {
    RecordMemoryPressure(last_pressure_level_, ticks_to_report);
  }

  // Save the now-current memory pressure level.
  last_pressure_level_ = new_pressure_level;
}

void MemoryPressureMonitor::UpdatePressureLevelOnRunLoopExit() {
  // Wait until it's time to check the pressure level.
  CFTimeInterval now = CFAbsoluteTimeGetCurrent();
  if (now >= next_run_loop_update_time_) {
    UpdatePressureLevel();

    // Update again in kUMATickSize seconds. We can update at any frequency,
    // but because we're only checking memory pressure levels for UMA there's
    // no need to update more frequently than we're keeping statistics on.
    next_run_loop_update_time_ = now + kUMATickSize - subtick_seconds_;
  }
}

// Static.
int MemoryPressureMonitor::GetSecondsPerUMATick() {
  return kUMATickSize;
}

MemoryPressureListener::MemoryPressureLevel
MemoryPressureMonitor::GetCurrentPressureLevel() {
  return last_pressure_level_;
}

void MemoryPressureMonitor::OnMemoryPressureChanged(
    dispatch_source_s* event_source,
    const MemoryPressureMonitor::DispatchCallback& dispatch_callback) {
  // The OS has sent a notification that the memory pressure level has changed.
  // Go through the normal memory pressure level checking mechanism so that
  // last_pressure_level_ and UMA get updated to the current value.
  UpdatePressureLevel();

  // Run the callback that's waiting on memory pressure change notifications.
  // The convention is to not send notifiations on memory pressure returning to
  // normal.
  if (last_pressure_level_ !=
      MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE)
    dispatch_callback.Run(last_pressure_level_);
}

void MemoryPressureMonitor::SetDispatchCallback(
    const DispatchCallback& callback) {
  dispatch_callback_ = callback;
}

}  // namespace mac
}  // namespace base
