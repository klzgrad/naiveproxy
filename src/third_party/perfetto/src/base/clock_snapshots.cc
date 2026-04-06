/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/ext/base/clock_snapshots.h"

#include "perfetto/base/build_config.h"
#include "perfetto/base/time.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"

namespace perfetto::base {

ClockSnapshotVector CaptureClockSnapshots() {
  ClockSnapshotVector snapshot_data;
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE) &&   \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&     \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_FREEBSD) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_NACL) &&    \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)
  struct {
    clockid_t id;
    protos::pbzero::BuiltinClock type;
    struct timespec ts;
  } clocks[] = {
      {CLOCK_BOOTTIME, protos::pbzero::BUILTIN_CLOCK_BOOTTIME, {0, 0}},
      {CLOCK_REALTIME_COARSE,
       protos::pbzero::BUILTIN_CLOCK_REALTIME_COARSE,
       {0, 0}},
      {CLOCK_MONOTONIC_COARSE,
       protos::pbzero::BUILTIN_CLOCK_MONOTONIC_COARSE,
       {0, 0}},
      {CLOCK_REALTIME, protos::pbzero::BUILTIN_CLOCK_REALTIME, {0, 0}},
      {CLOCK_MONOTONIC, protos::pbzero::BUILTIN_CLOCK_MONOTONIC, {0, 0}},
      {CLOCK_MONOTONIC_RAW,
       protos::pbzero::BUILTIN_CLOCK_MONOTONIC_RAW,
       {0, 0}},
  };
  // First snapshot all the clocks as atomically as we can.
  for (auto& clock : clocks) {
    if (clock_gettime(clock.id, &clock.ts) == -1)
      PERFETTO_DLOG("clock_gettime failed for clock %d", clock.id);
  }
  for (auto& clock : clocks) {
    snapshot_data.push_back(ClockReading(
        static_cast<uint32_t>(clock.type),
        static_cast<uint64_t>(base::FromPosixTimespec(clock.ts).count())));
  }
#else  // OS_APPLE || OS_WIN && OS_NACL
  auto wall_time_ns = static_cast<uint64_t>(base::GetWallTimeNs().count());
  // The default trace clock is boot time, so we always need to emit a path to
  // it. However since we don't actually have a boot time source on these
  // platforms, pretend that wall time equals boot time.
  snapshot_data.push_back(
      ClockReading(protos::pbzero::BUILTIN_CLOCK_BOOTTIME, wall_time_ns));
  snapshot_data.push_back(
      ClockReading(protos::pbzero::BUILTIN_CLOCK_MONOTONIC, wall_time_ns));
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_X86_64)
  // X86-specific but OS-independent TSC clocksource
  snapshot_data.push_back(
      ClockReading(protos::pbzero::BUILTIN_CLOCK_TSC, base::Rdtsc()));
#endif  // PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_X86_64)

  return snapshot_data;
}

}  // namespace perfetto::base
