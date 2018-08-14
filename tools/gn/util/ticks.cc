// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ticks.h"

#include "base/logging.h"
#include "build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#elif defined(OS_MACOSX)
#include <mach/mach_time.h>
#elif defined(OS_LINUX) || defined(OS_AIX)
#include <time.h>
#else
#error Port.
#endif

namespace {

bool g_initialized;

#if defined(OS_WIN)
LARGE_INTEGER g_frequency;
LARGE_INTEGER g_start;
#elif defined(OS_MACOSX)
mach_timebase_info_data_t g_timebase;
uint64_t g_start;
#elif defined(OS_LINUX) || defined(OS_AIX)
uint64_t g_start;
#else
#error Port.
#endif

constexpr uint64_t kNano = 1'000'000'000;

void Init() {
  DCHECK(!g_initialized);

#if defined(OS_WIN)
  QueryPerformanceFrequency(&g_frequency);
  QueryPerformanceCounter(&g_start);
#elif defined(OS_MACOSX)
  mach_timebase_info(&g_timebase);
  g_start = (mach_absolute_time() * g_timebase.numer) / g_timebase.denom;
#elif defined(OS_LINUX) || defined(OS_AIX)
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  g_start = static_cast<uint64_t>(ts.tv_sec) * kNano +
            static_cast<uint64_t>(ts.tv_nsec);
#else
#error Port.
#endif

  g_initialized = true;
}

}  // namespace

Ticks TicksNow() {
  static bool initialized = []() {
    Init();
    return true;
  }();
  DCHECK(initialized);

  Ticks now;

#if defined(OS_WIN)
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  now = ((t.QuadPart - g_start.QuadPart) * kNano) / g_frequency.QuadPart;
#elif defined(OS_MACOSX)
  now =
      ((mach_absolute_time() * g_timebase.numer) / g_timebase.denom) - g_start;
#elif defined(OS_LINUX) || defined(OS_AIX)
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  now = (static_cast<uint64_t>(ts.tv_sec) * kNano +
         static_cast<uint64_t>(ts.tv_nsec)) -
        g_start;
#else
#error Port.
#endif

  return now;
}

TickDelta TicksDelta(Ticks new_ticks, Ticks old_ticks) {
  DCHECK(new_ticks >= old_ticks);
  return TickDelta(new_ticks - old_ticks);
}
