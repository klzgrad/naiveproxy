// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/protected_memory.h"

#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(OS_LINUX)
#include <sys/resource.h>
#endif  // defined(OS_LINUX)

#if defined(OS_MACOSX) && !defined(OS_IOS)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif  // defined(OS_MACOSX) && !defined(OS_IOS)

#include "base/posix/eintr_wrapper.h"
#include "base/process/process_metrics.h"
#include "base/synchronization/lock.h"
#include "build/build_config.h"

namespace base {

namespace {

bool SetMemory(void* start, void* end, int prot) {
  DCHECK(end > start);
  const uintptr_t page_mask = ~(base::GetPageSize() - 1);
  const uintptr_t page_start = reinterpret_cast<uintptr_t>(start) & page_mask;
  return mprotect(reinterpret_cast<void*>(page_start),
                  reinterpret_cast<uintptr_t>(end) - page_start, prot) == 0;
}

}  // namespace

bool AutoWritableMemory::SetMemoryReadWrite(void* start, void* end) {
  return SetMemory(start, end, PROT_READ | PROT_WRITE);
}

bool AutoWritableMemory::SetMemoryReadOnly(void* start, void* end) {
  return SetMemory(start, end, PROT_READ);
}

#if defined(OS_LINUX)
void AssertMemoryIsReadOnly(const void* ptr) {
#if DCHECK_IS_ON()
  const uintptr_t page_mask = ~(base::GetPageSize() - 1);
  const uintptr_t page_start = reinterpret_cast<uintptr_t>(ptr) & page_mask;

  // Note: We've casted away const here, which should not be meaningful since
  // if the memory is written to we will abort immediately.
  int result =
      getrlimit(RLIMIT_NPROC, reinterpret_cast<struct rlimit*>(page_start));
  DCHECK_EQ(result, -1);
  DCHECK_EQ(errno, EFAULT);
#endif  // DCHECK_IS_ON()
}
#elif defined(OS_MACOSX) && !defined(OS_IOS)
void AssertMemoryIsReadOnly(const void* ptr) {
#if DCHECK_IS_ON()
  mach_port_t object_name;
  vm_region_basic_info_64 region_info;
  mach_vm_size_t size = 1;
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;

  kern_return_t kr = mach_vm_region(
      mach_task_self(), reinterpret_cast<mach_vm_address_t*>(&ptr), &size,
      VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&region_info),
      &count, &object_name);
  DCHECK_EQ(kr, KERN_SUCCESS);
  DCHECK_EQ(region_info.protection, VM_PROT_READ);
#endif  // DCHECK_IS_ON()
}
#endif  // defined(OS_LINUX) || (defined(OS_MACOSX) && !defined(OS_IOS))

}  // namespace base
