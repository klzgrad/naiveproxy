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

#include "src/trace_processor/importers/syscalls/syscall_tracker.h"

#include <cinttypes>
#include <type_traits>
#include <utility>

#include "perfetto/ext/base/string_utils.h"
#include "src/kernel_utils/syscall_table.h"
#include "src/trace_processor/storage/stats.h"

namespace perfetto::trace_processor {

// TODO(primiano): The current design is broken in case of 32-bit processes
// running on 64-bit kernel. At least on ARM, the syscal numbers don't match
// and we should use the kSyscalls_Aarch32 table for those processes. But this
// means that the architecture is not a global property but is per-process.
// Which in turn means that somehow we need to figure out what is the bitness
// of each process from the trace.
SyscallTracker::SyscallTracker(TraceProcessorContext* context)
    : context_(context) {
  SetArchitecture(Architecture::kUnknown);
}

SyscallTracker::~SyscallTracker() = default;

void SyscallTracker::SetArchitecture(Architecture arch) {
  SyscallTable syscalls(arch);

  for (size_t i = 0; i < kMaxSyscalls; i++) {
    StringId id = kNullStringId;
    const char* name = syscalls.GetById(i);
    if (name && *name) {
      id = context_->storage->InternString(name);
      if (!strcmp(name, "sys_write")) {
        sys_write_string_id_ = id;
      } else if (!strcmp(name, "sys_rt_sigreturn")) {
        sys_rt_sigreturn_string_id_ = id;
      }
    } else {
      base::StackString<64> unknown_str("sys_%zu", i);
      id = context_->storage->InternString(unknown_str.string_view());
    }
    arch_syscall_to_string_id_[i] = id;
  }
}

}  // namespace perfetto::trace_processor
