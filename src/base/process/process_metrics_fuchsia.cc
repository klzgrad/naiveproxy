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
  // TODO(https://crbug.com/926581): Fuchsia does not support this.
  NOTIMPLEMENTED_LOG_ONCE();
  return 0;
}

// static
std::unique_ptr<ProcessMetrics> ProcessMetrics::CreateProcessMetrics(
    ProcessHandle process) {
  NOTIMPLEMENTED_LOG_ONCE();  // TODO(https://crbug.com/926581).
  return nullptr;
}

TimeDelta ProcessMetrics::GetCumulativeCPUUsage() {
  NOTIMPLEMENTED_LOG_ONCE();  // TODO(https://crbug.com/926581).
  return TimeDelta();
}

bool GetSystemMemoryInfo(SystemMemoryInfoKB* meminfo) {
  NOTIMPLEMENTED_LOG_ONCE();  // TODO(https://crbug.com/926581).
  return false;
}

}  // namespace base
