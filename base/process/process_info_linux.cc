// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_info.h"

#include <stdint.h>

#include "base/logging.h"
#include "base/process/internal_linux.h"
#include "base/process/process_handle.h"
#include "base/time/time.h"

namespace base {

// static
const Time CurrentProcessInfo::CreationTime() {
  int64_t start_ticks =
      internal::ReadProcSelfStatsAndGetFieldAsInt64(internal::VM_STARTTIME);
  if (!start_ticks)
    return Time();
  TimeDelta start_offset = internal::ClockTicksToTimeDelta(start_ticks);
  Time boot_time = internal::GetBootTime();
  if (boot_time.is_null())
    return Time();
  return Time(boot_time + start_offset);
}

}  // namespace base
