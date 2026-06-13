/*
 * Copyright (C) 2022 The Android Open Source Project
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
#include "src/kernel_utils/syscall_table.h"

#include "perfetto/base/build_config.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <sys/utsname.h>
#endif

#include "src/kernel_utils/syscall_table_generated.h"

namespace perfetto {

SyscallTable::SyscallTable(Architecture arch) {
  switch (arch) {
    case Architecture::kArm64:
      *this = SyscallTable::Load<SyscallTable_arm64>();
      break;
    case Architecture::kArm32:
      *this = SyscallTable::Load<SyscallTable_arm32>();
      break;
    case Architecture::kX86_64:
      *this = SyscallTable::Load<SyscallTable_x86_64>();
      break;
    case Architecture::kX86:
      *this = SyscallTable::Load<SyscallTable_x86>();
      break;
    case Architecture::kUnknown:
      // The default field initializers take care of the null initialization.
      break;
  }
}

Architecture SyscallTable::ArchFromString(base::StringView machine) {
  if (machine == "aarch64") {
    return Architecture::kArm64;
  } else if (machine == "armv8l" || machine == "armv7l") {
    // armv8l is a 32 bit userspace process on a 64 bit kernel
    return Architecture::kArm32;
  } else if (machine == "x86_64") {
    return Architecture::kX86_64;
  } else if (machine == "i686") {
    return Architecture::kX86;
  } else {
    return Architecture::kUnknown;
  }
}

SyscallTable SyscallTable::FromCurrentArch() {
  Architecture arch = Architecture::kUnknown;

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  struct utsname uname_info;
  if (uname(&uname_info) == 0) {
    arch = ArchFromString(uname_info.machine);
  }
#endif

  return SyscallTable(arch);
}

std::optional<size_t> SyscallTable::GetByName(const std::string& name) const {
  for (size_t i = 0; i < syscall_count_; i++) {
    if (name == &syscall_names_[syscall_offsets_[i]]) {
      return i;
    }
  }
  return std::nullopt;
}

const char* SyscallTable::GetById(size_t id) const {
  if (id < syscall_count_) {
    return &syscall_names_[syscall_offsets_[id]];
  }
  return nullptr;
}

}  // namespace perfetto
