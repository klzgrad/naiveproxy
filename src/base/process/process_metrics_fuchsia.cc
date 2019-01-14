// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_metrics.h"

#include <lib/fdio/limits.h>

namespace base {

size_t GetMaxFds() {
  return FDIO_MAX_FD;
}

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

TimeDelta ProcessMetrics::GetCumulativeCPUUsage() {
  NOTIMPLEMENTED();  // TODO(fuchsia): https://crbug.com/706592.
  return TimeDelta();
}

bool GetSystemMemoryInfo(SystemMemoryInfoKB* meminfo) {
  NOTIMPLEMENTED();  // TODO(fuchsia): https://crbug.com/706592.
  return false;
}

}  // namespace base
