// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ALLOCATOR_PARTITION_ALLOCATOR_ADDRESS_SPACE_RANDOMIZATION
#define BASE_ALLOCATOR_PARTITION_ALLOCATOR_ADDRESS_SPACE_RANDOMIZATION

#include "base/allocator/partition_allocator/page_allocator.h"
#include "base/base_export.h"
#include "build/build_config.h"

namespace base {

namespace internal {

constexpr uintptr_t AslrAddress(uintptr_t mask) {
  return mask & kPageAllocationGranularityBaseMask;
}
constexpr uintptr_t AslrMask(uintptr_t bits) {
  return AslrAddress((1ULL << bits) - 1ULL);
}

#if defined(ARCH_CPU_64_BITS)
#if defined(MEMORY_TOOL_REPLACES_ALLOCATOR)
// We shouldn't be allocating system pages at all for sanitizer builds. However,
// we do, and if random hint addresses interfere with address ranges hard coded
// in those tools, bad things happen. This address range is copied from TSAN
// source but works with all tools.
// See crbug.com/539863.
constexpr uintptr_t kASLRMask = AslrAddress(0x007fffffffffULL);
constexpr uintptr_t kASLROffset = AslrAddress(0x7e8000000000ULL);
#elif defined(OS_WIN)
// Windows 8.10 and newer support the full 48 bit address range. Older versions
// of Windows only support 44 bits. Since kASLROffset is non-zero and may cause
// a carry, use 47 and 43 bit masks.
// See http://www.alex-ionescu.com/?p=246
constexpr uintptr_t kASLRMask = AslrMask(47);
constexpr uintptr_t kASLRMaskBefore8_10 = AslrMask(43);
// Try not to map pages into the range where Windows loads DLLs by default.
constexpr uintptr_t kASLROffset = 0x80000000ULL;
#elif defined(OS_MACOSX)
// macOS as of 10.12.5 does not clean up entries in page map levels 3/4
// [PDP/PML4] created from mmap or mach_vm_allocate, even after the region is
// destroyed. Using a virtual address space that is too large causes a leak of
// about 1 wired [can never be paged out] page per call to mmap(). The page is
// only reclaimed when the process is killed. Confine the hint to a 39-bit
// section of the virtual address space.
//
// This implementation adapted from
// https://chromium-review.googlesource.com/c/v8/v8/+/557958. The difference
// is that here we clamp to 39 bits, not 32.
//
// TODO(crbug.com/738925): Remove this limitation if/when the macOS behavior
// changes.
constexpr uintptr_t kASLRMask = AslrMask(38);
constexpr uintptr_t kASLROffset = AslrAddress(0x1000000000ULL);
#else  // defined(OS_POSIX)
#if defined(ARCH_CPU_X86_64)
// Linux and OS X support the full 47-bit user space of x64 processors. Use
// only 46 to allow kernel a chance to fulfill request.
constexpr uintptr_t kASLRMask = AslrMask(46);
constexpr uintptr_t kASLROffset = AslrAddress(0);
#elif defined(ARCH_CPU_ARM64)
// ARM64 on Linux has 39-bit user space. Use 38 bits since kASLROffset could
// cause a carry.
constexpr uintptr_t kASLRMask = AslrMask(38);
constexpr uintptr_t kASLROffset = AslrAddress(0x1000000000ULL);
#elif defined(ARCH_CPU_PPC64)
#if defined(OS_AIX)
// AIX: 64 bits of virtual addressing, but we limit address range to:
//   a) minimize Segment Lookaside Buffer (SLB) misses and
//   b) use extra address space to isolate the mmap regions.
constexpr uintptr_t kASLRMask = AslrMask(30);
constexpr uintptr_t kASLROffset = AslrAddress(0x400000000000ULL);
#elif defined(ARCH_CPU_BIG_ENDIAN)
// Big-endian Linux: 44 bits of virtual addressing. Use 42.
constexpr uintptr_t kASLRMask = AslrMask(42);
constexpr uintptr_t kASLROffset = AslrAddress(0);
#else   // !defined(OS_AIX) && !defined(ARCH_CPU_BIG_ENDIAN)
// Little-endian Linux: 48 bits of virtual addressing. Use 46.
constexpr uintptr_t kASLRMask = AslrMask(46);
constexpr uintptr_t kASLROffset = AslrAddress(0);
#endif  // !defined(OS_AIX) && !defined(ARCH_CPU_BIG_ENDIAN)
#elif defined(ARCH_CPU_S390X)
// Linux on Z uses bits 22-32 for Region Indexing, which translates to 42 bits
// of virtual addressing.  Truncate to 40 bits to allow kernel chance to
// fulfill request.
constexpr uintptr_t kASLRMask = AslrMask(40);
constexpr uintptr_t kASLROffset = AslrAddress(0);
#elif defined(ARCH_CPU_S390)
// 31 bits of virtual addressing.  Truncate to 29 bits to allow kernel a chance
// to fulfill request.
constexpr uintptr_t kASLRMask = AslrMask(29);
constexpr uintptr_t kASLROffset = AslrAddress(0);
#else  // !defined(ARCH_CPU_X86_64) && !defined(ARCH_CPU_PPC64) &&
// !defined(ARCH_CPU_S390X) && !defined(ARCH_CPU_S390)
// All other POSIX variants, use 30 bits.
constexpr uintptr_t kASLRMask = AslrMask(30);
#if defined(OS_SOLARIS)
// For our Solaris/illumos mmap hint, we pick a random address in the bottom
// half of the top half of the address space (that is, the third quarter).
// Because we do not MAP_FIXED, this will be treated only as a hint -- the
// system will not fail to mmap() because something else happens to already
// be mapped at our random address. We deliberately set the hint high enough
// to get well above the system's break (that is, the heap); Solaris and
// illumos will try the hint and if that fails allocate as if there were
// no hint at all. The high hint prevents the break from getting hemmed in
// at low values, ceding half of the address space to the system heap.
constexpr uintptr_t kASLROffset = AslrAddress(0x80000000ULL);
#elif defined(OS_AIX)
// The range 0x30000000 - 0xD0000000 is available on AIX;
// choose the upper range.
constexpr uintptr_t kASLROffset = AslrAddress(0x90000000ULL);
#else   // !defined(OS_SOLARIS) && !defined(OS_AIX)
// The range 0x20000000 - 0x60000000 is relatively unpopulated across a
// variety of ASLR modes (PAE kernel, NX compat mode, etc) and on macos
// 10.6 and 10.7.
constexpr uintptr_t kASLROffset = AslrAddress(0x20000000ULL);
#endif  // !defined(OS_SOLARIS) && !defined(OS_AIX)
#endif  // !defined(ARCH_CPU_X86_64) && !defined(ARCH_CPU_PPC64) &&
// !defined(ARCH_CPU_S390X) && !defined(ARCH_CPU_S390)
#endif  // defined(OS_POSIX)
#else   // defined(ARCH_CPU_32_BITS)
// This is a good range on 32 bit Windows, Linux and Mac.
// Allocates in the 0.5-1.5GB region. There is no issue with carries here.
constexpr uintptr_t kASLRMask = AslrMask(30);
constexpr uintptr_t kASLROffset = AslrAddress(0x20000000ULL);
#endif  // defined(ARCH_CPU_32_BITS)

}  // namespace internal

// Calculates a random preferred mapping address. In calculating an address, we
// balance good ASLR against not fragmenting the address space too badly.
BASE_EXPORT void* GetRandomPageBase();

}  // namespace base

#endif  // BASE_ALLOCATOR_PARTITION_ALLOCATOR_ADDRESS_SPACE_RANDOMIZATION
