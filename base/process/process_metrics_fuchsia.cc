// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_metrics.h"

namespace base {

size_t GetSystemCommitCharge() {
  // Not available, doesn't seem likely that it will be (for the whole system).
  NOTIMPLEMENTED();
  return 0;
}

// static
std::unique_ptr<ProcessMetrics> ProcessMetrics::CreateProcessMetrics(
    ProcessHandle process) {
  NOTIMPLEMENTED();  // TODO(fuchsia): https://crbug.com/706592.
  return nullptr;
}

double ProcessMetrics::GetPlatformIndependentCPUUsage() {
  NOTIMPLEMENTED();  // TODO(fuchsia): https://crbug.com/706592.
  return 0.0;
}

size_t ProcessMetrics::GetPagefileUsage() const {
  NOTIMPLEMENTED();  // TODO(fuchsia): https://crbug.com/706592.
  return 0;
}

size_t ProcessMetrics::GetWorkingSetSize() const {
  NOTIMPLEMENTED();  // TODO(fuchsia): https://crbug.com/706592.
  return 0;
}

size_t ProcessMetrics::GetPeakWorkingSetSize() const {
  NOTIMPLEMENTED();  // TODO(fuchsia): https://crbug.com/706592.
  return 0;
}

bool ProcessMetrics::GetWorkingSetKBytes(WorkingSetKBytes* ws_usage) const {
  NOTIMPLEMENTED();  // TODO(fuchsia): https://crbug.com/706592.
  return false;
}

bool GetSystemMemoryInfo(SystemMemoryInfoKB* meminfo) {
  NOTIMPLEMENTED();  // TODO(fuchsia): https://crbug.com/706592.
  return false;
}

}  // namespace base
