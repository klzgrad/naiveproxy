// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_MEMORY_PRESSURE_MONITOR_MAC_H_
#define BASE_MEMORY_MEMORY_PRESSURE_MONITOR_MAC_H_

#include <CoreFoundation/CFDate.h>
#include <dispatch/dispatch.h>

#include "base/base_export.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/mac/scoped_dispatch_object.h"
#include "base/macros.h"
#include "base/memory/memory_pressure_listener.h"
#include "base/memory/memory_pressure_monitor.h"
#include "base/message_loop/message_pump_mac.h"

namespace base {
namespace mac {

class TestMemoryPressureMonitor;

// Declares the interface for the Mac MemoryPressureMonitor, which reports
// memory pressure events and status.
class BASE_EXPORT MemoryPressureMonitor : public base::MemoryPressureMonitor {
 public:
  MemoryPressureMonitor();
  ~MemoryPressureMonitor() override;

  // Returns the currently-observed memory pressure.
  MemoryPressureLevel GetCurrentPressureLevel() override;

  void SetDispatchCallback(const DispatchCallback& callback) override;

 private:
  friend TestMemoryPressureMonitor;

  static MemoryPressureLevel MemoryPressureLevelForMacMemoryPressureLevel(
      int mac_memory_pressure_level);
  static void OnRunLoopExit(CFRunLoopObserverRef observer,
                            CFRunLoopActivity activity,
                            void* info);
  // Returns the raw memory pressure level from the macOS. Exposed for
  // unit testing.
  virtual int GetMacMemoryPressureLevel();

  // Updates |last_pressure_level_| with the current memory pressure level.
  void UpdatePressureLevel();

  // Updates |last_pressure_level_| at the end of every run loop pass (modulo
  // some number of seconds).
  void UpdatePressureLevelOnRunLoopExit();

  // Run |dispatch_callback| on memory pressure notifications from the OS.
  void OnMemoryPressureChanged(dispatch_source_s* event_source,
                               const DispatchCallback& dispatch_callback);

  // Returns the number of seconds per UMA tick (for statistics recording).
  // Exposed for testing.
  static int GetSecondsPerUMATick();

  // The dispatch source that generates memory pressure change notifications.
  ScopedDispatchObject<dispatch_source_t> memory_level_event_source_;

  // The callback to call upon receiving a memory pressure change notification.
  DispatchCallback dispatch_callback_;

  // Last UMA report time.
  CFTimeInterval last_statistic_report_time_;

  // Most-recent memory pressure level.
  MemoryPressureLevel last_pressure_level_;

  // Observer that tracks exits from the main run loop.
  ScopedCFTypeRef<CFRunLoopObserverRef> exit_observer_;

  // Next time to update the memory pressure level when exiting the run loop.
  CFTimeInterval next_run_loop_update_time_;

  // Seconds left over from the last UMA tick calculation (to be added to the
  // next calculation).
  CFTimeInterval subtick_seconds_;

  DISALLOW_COPY_AND_ASSIGN(MemoryPressureMonitor);
};

}  // namespace mac
}  // namespace base

#endif  // BASE_MEMORY_MEMORY_PRESSURE_MONITOR_MAC_H_
