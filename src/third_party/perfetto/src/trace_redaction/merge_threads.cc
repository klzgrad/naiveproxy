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

#include "src/trace_redaction/merge_threads.h"

namespace perfetto::trace_redaction {

void MergeThreadsPids::Modify(const Context& context,
                              uint64_t ts,
                              int32_t cpu,
                              int32_t* pid,
                              std::string* comm) const {
  // When Modify() is used with RedactFtraceEvents, comm will be null.
  PERFETTO_DCHECK(pid);

  // Avoid re-mapping system threads (pid 0). These pids have special uses (e.g.
  // cpu_idle) and if re-mapped, important structures break (e.g. remapping
  // cpu_idle's pid breaks scheduling).

  if (*pid == 0) {
    return;
  }

  if (context.timeline->PidConnectsToUid(ts, *pid, *context.package_uid)) {
    return;
  }

  *pid = context.synthetic_process->RunningOn(cpu);

  if (comm) {
    comm->clear();
  }
}

}  // namespace perfetto::trace_redaction
