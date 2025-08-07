/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/types/task_state.h"

#include <string.h>

#include "perfetto/base/logging.h"

namespace perfetto {
namespace trace_processor {
namespace ftrace_utils {

// static
TaskState TaskState::FromRawPrevState(
    uint16_t raw_state,
    std::optional<VersionNumber> kernel_version) {
  return TaskState(raw_state, kernel_version);
}

// static
TaskState TaskState::FromSystrace(const char* state_str) {
  return TaskState(state_str);
}

// static
TaskState TaskState::FromParsedFlags(uint16_t parsed_state) {
  TaskState ret;
  ret.parsed_ = parsed_state;
  return ret;
}

// See header for extra details.
//
// Note to maintainers: going forward, the most likely "breaking" changes are:
// * a new flag is added to TASK_REPORT (see include/linux/sched.h kernel src)
// * a new report-specific flag is added above TASK_REPORT
// In both cases, this will change the value of TASK_REPORT_MAX that is used to
// report preemption in sched_switch. We'll need to modify this class to keep
// up, or make traced_probes record the sched_switch format string in traces.
//
// Note to maintainers: if changing the default kernel assumption or the 4.4
// codepath, you'll need to update ToRawStateOnlyForSystraceConversions().
TaskState::TaskState(uint16_t raw_state,
                     std::optional<VersionNumber> opt_version) {
  // Values up to and including 0x20 (EXIT_ZOMBIE) never changed, so map them
  // directly onto ParsedFlag (we use the same flag bits for convenience).
  parsed_ = raw_state & (0x40 - 1);

  // Parsing upper bits depends on kernel version. Default to 4.4 because old
  // perfetto traces don't record kernel version.
  auto version = VersionNumber{4, 4};
  if (opt_version) {
    version = opt_version.value();
  }

  // Kernels 4.14+: flags up to and including 0x40 (TASK_PARKED) are reported
  // with their scheduler values. Whereas flags 0x80 (normally TASK_DEAD) and
  // above are masked off and repurposed for reporting-specific values.
  if (version >= VersionNumber{4, 14}) {
    if (raw_state & 0x40)  // TASK_PARKED
      parsed_ |= kParked;

    // REPORT_TASK_IDLE (0x80), which reports the TASK_IDLE composite state
    // (TASK_UNINTERRUPTIBLE | TASK_NOLOAD):
    if (raw_state & 0x80) {
      parsed_ |= kIdle;
    }

    // REPORT_TASK_MAX that sched_switch uses to report preemption. At the time
    // of writing 0x100 because REPORT_TASK_IDLE is the only report-specific
    // flag:
    if (raw_state & 0x100)
      parsed_ |= kPreempted;

    // Attempt to notice REPORT_TASK_MAX changing. If this dcheck fires, please
    // file a bug report against perfetto. Exactly 4.14 kernels are excluded
    // from the dcheck since there are known instances of such kernels that
    // still use the old flag mask in practice. So we'll still mark the states
    // as invalid but not crash debug builds.
    if (raw_state & 0xfe00) {
      parsed_ = kInvalid;
      PERFETTO_DCHECK((version == VersionNumber{4, 14}));
    }
    return;
  }

  // Before 4.14, sched_switch reported the full set of scheduler flags
  // (without masking down to TASK_REPORT). Note: several flags starting at
  // 0x40 have a different value to the above because 4.14 reordered them.
  // See https://github.com/torvalds/linux/commit/8ef9925b02.
  if (raw_state & 0x40)  // TASK_DEAD
    parsed_ |= kTaskDead;
  if (raw_state & 0x80)  // TASK_WAKEKILL
    parsed_ |= kWakeKill;
  if (raw_state & 0x100)  // TASK_WAKING
    parsed_ |= kWaking;
  if (raw_state & 0x200)  // TASK_PARKED
    parsed_ |= kParked;
  if (raw_state & 0x400)  // TASK_NOLOAD
    parsed_ |= kNoLoad;

  // Convert kUninterruptibleSleep+kNoLoad into kIdle since that's what it
  // means, and the UI can present the latter better.
  // See https://github.com/torvalds/linux/commit/80ed87c8a9ca.
  if (parsed_ == (kUninterruptibleSleep | kNoLoad)) {
    parsed_ = kIdle;
  }

  // Kernel version range [4.8, 4.14) has TASK_NEW, hence preemption
  // (TASK_STATE_MAX) is 0x1000. We don't decode TASK_NEW itself since it will
  // never show up in sched_switch.
  if (version >= VersionNumber{4, 8}) {
    if (raw_state & 0x1000)
      parsed_ |= kPreempted;
  } else {
    // Kernel (..., 4.8), preemption (TASK_STATE_MAX) is 0x800. Assume all
    // kernels in this range have the 4.4 state of the bitmask. This is most
    // likely incorrect on <4.2 as that's when TASK_NOLOAD was introduced
    // (which means preemption is reported at a different bit).
    if (raw_state & 0x800)
      parsed_ |= kPreempted;
  }
}

TaskState::TaskStateStr TaskState::ToString(char separator) const {
  if (!is_valid()) {
    return TaskStateStr{"?"};
  }

  // Character aliases follow sched_switch's format string.
  char buffer[32];
  size_t pos = 0;
  if (is_runnable()) {
    buffer[pos++] = 'R';
    if (parsed_ & kPreempted) {
      buffer[pos++] = '+';
      PERFETTO_DCHECK(parsed_ == kPreempted);
    }
  } else {
    auto append = [&](ParsedFlag flag, char c) {
      if (!(parsed_ & flag))
        return;
      if (separator && pos != 0)
        buffer[pos++] = separator;
      buffer[pos++] = c;
    };
    append(kInterruptibleSleep, 'S');
    append(kUninterruptibleSleep, 'D');  // (D)isk sleep
    append(kStopped, 'T');
    append(kTraced, 't');
    append(kExitDead, 'X');
    append(kExitZombie, 'Z');
    append(kParked, 'P');
    append(kTaskDead, 'x');
    append(kWakeKill, 'K');
    append(kWaking, 'W');
    append(kNoLoad, 'N');
    append(kIdle, 'I');
  }

  TaskStateStr output{};
  size_t sz = (pos < output.size() - 1) ? pos : output.size() - 1;
  memcpy(output.data(), buffer, sz);
  return output;
}

// Used when parsing systrace, i.e. textual ftrace output.
TaskState::TaskState(const char* state_str) {
  parsed_ = 0;
  if (!state_str || state_str[0] == '\0') {
    parsed_ = kInvalid;
    return;
  }

  // R or R+, otherwise invalid
  if (state_str[0] == 'R') {
    parsed_ = kRunnable;
    if (!strncmp(state_str, "R+", 3))
      parsed_ |= kPreempted;
    return;
  }

  for (size_t i = 0; state_str[i] != '\0'; i++) {
    char c = state_str[i];
    if (c == 'R' || c == '+') {
      parsed_ = kInvalid;
      return;
    }
    if (c == '|')
      continue;

    auto parse = [&](ParsedFlag flag, char symbol) {
      if (c == symbol)
        parsed_ |= flag;
      return c == symbol;
    };
    bool recognized = false;
    recognized |= parse(kInterruptibleSleep, 'S');
    recognized |= parse(kUninterruptibleSleep, 'D');  // (D)isk sleep
    recognized |= parse(kStopped, 'T');
    recognized |= parse(kTraced, 't');
    recognized |= parse(kExitDead, 'X');
    recognized |= parse(kExitZombie, 'Z');
    recognized |= parse(kParked, 'P');
    recognized |= parse(kTaskDead, 'x');
    recognized |= parse(kWakeKill, 'K');
    recognized |= parse(kWaking, 'W');
    recognized |= parse(kNoLoad, 'N');
    recognized |= parse(kIdle, 'I');
    if (!recognized) {
      parsed_ = kInvalid;
      return;
    }
  }
}

// Hard-assume 4.4 flag layout per the header rationale.
uint16_t TaskState::ToRawStateOnlyForSystraceConversions() const {
  if (parsed_ == kInvalid)
    return 0xffff;

  if (parsed_ == kPreempted)
    return 0x0800;

  uint16_t ret = parsed_ & (0x40 - 1);
  if (parsed_ & kTaskDead)
    ret |= 0x40;
  if (parsed_ & kWakeKill)
    ret |= 0x80;
  if (parsed_ & kWaking)
    ret |= 0x100;
  if (parsed_ & kParked)
    ret |= 0x200;
  if (parsed_ & kNoLoad)
    ret |= 0x400;

  // Expand kIdle into the underlying kUninterruptibleSleep + kNoLoad.
  if (parsed_ & kIdle)
    ret |= (0x2 | 0x400);

  return ret;
}

}  // namespace ftrace_utils
}  // namespace trace_processor
}  // namespace perfetto
