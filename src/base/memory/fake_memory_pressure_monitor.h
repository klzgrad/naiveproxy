// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_FAKE_MEMORY_PRESSURE_MONITOR_H_
#define BASE_MEMORY_FAKE_MEMORY_PRESSURE_MONITOR_H_

#include "base/macros.h"
#include "base/memory/memory_pressure_monitor.h"

namespace base {
namespace test {

class FakeMemoryPressureMonitor : public base::MemoryPressureMonitor {
 public:
  FakeMemoryPressureMonitor();
  ~FakeMemoryPressureMonitor() override;

  void SetAndNotifyMemoryPressure(MemoryPressureLevel level);

  // base::MemoryPressureMonitor overrides:
  MemoryPressureLevel GetCurrentPressureLevel() override;
  void SetDispatchCallback(const DispatchCallback& callback) override;

 private:
  MemoryPressureLevel memory_pressure_level_;

  DISALLOW_COPY_AND_ASSIGN(FakeMemoryPressureMonitor);
};

}  // namespace test
}  // namespace base

#endif  // BASE_MEMORY_FAKE_MEMORY_PRESSURE_MONITOR_H_
