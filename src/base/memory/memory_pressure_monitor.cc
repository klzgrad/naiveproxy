// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/memory_pressure_monitor.h"

#include "base/check.h"
#include "base/metrics/histogram.h"
#include "base/notreached.h"

namespace base {
namespace {

MemoryPressureMonitor* g_monitor = nullptr;

}  // namespace

const base::TimeDelta MemoryPressureMonitor::kUMAMemoryPressureLevelPeriod =
    base::TimeDelta::FromSeconds(5);

MemoryPressureMonitor::MemoryPressureMonitor() {
  DCHECK(!g_monitor);
  g_monitor = this;
}

MemoryPressureMonitor::~MemoryPressureMonitor() {
  DCHECK(g_monitor);
  g_monitor = nullptr;
}

// static
MemoryPressureMonitor* MemoryPressureMonitor::Get() {
  return g_monitor;
}

void MemoryPressureMonitor::RecordMemoryPressure(
    base::MemoryPressureListener::MemoryPressureLevel level,
    int ticks) {
  // We can't use UmaHistogramEnumeration here as it doesn't support |AddCount|.
  base::LinearHistogram::FactoryGet(
      "Memory.PressureLevel", 1,
      MemoryPressureListener::MemoryPressureLevel::kMaxValue + 1,
      MemoryPressureListener::MemoryPressureLevel::kMaxValue + 2,
      base::HistogramBase::kUmaTargetedHistogramFlag)
      ->AddCount(level, ticks);
}

}  // namespace base
