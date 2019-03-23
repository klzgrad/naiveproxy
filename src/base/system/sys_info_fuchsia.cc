// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/system/sys_info.h"

#include <zircon/syscalls.h>

#include "base/logging.h"

namespace base {

// static
int64_t SysInfo::AmountOfPhysicalMemoryImpl() {
  return zx_system_get_physmem();
}

// static
int64_t SysInfo::AmountOfAvailablePhysicalMemoryImpl() {
  // TODO(fuchsia): https://crbug.com/706592 This is not exposed.
  NOTREACHED();
  return 0;
}

// static
int SysInfo::NumberOfProcessors() {
  return zx_system_get_num_cpus();
}

// static
int64_t SysInfo::AmountOfVirtualMemory() {
  return 0;
}

}  // namespace base
