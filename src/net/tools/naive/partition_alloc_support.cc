// Copyright 2021 The Chromium Authors
// Copyright 2022 klzgrad <kizdiv@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/partition_alloc_support.h"

#include "base/allocator/allocator_check.h"
#include "base/allocator/partition_alloc_support.h"
#include "base/allocator/partition_allocator/shim/allocator_shim.h"
#include "base/check.h"
#include "base/process/memory.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_APPLE)
#include "base/allocator/early_zone_registration_mac.h"
#endif

namespace naive_partition_alloc_support {

void ReconfigureEarly() {
  // chrome/app/chrome_exe_main_mac.cc: main()
#if BUILDFLAG(IS_APPLE)
  partition_alloc::EarlyMallocZoneRegistration();
#endif

  // content/app/content_main.cc: RunContentProcess()
#if BUILDFLAG(IS_APPLE) && BUILDFLAG(USE_ALLOCATOR_SHIM)
  // The static initializer function for initializing PartitionAlloc
  // InitializeDefaultMallocZoneWithPartitionAlloc() would be removed by the
  // linker if allocator_shim.o is not referenced by the following call,
  // resulting in undefined behavior of accessing uninitialized TLS
  // data in PurgeCurrentThread() when PA is enabled.
  allocator_shim::InitializeAllocatorShim();
#endif

  // content/app/content_main.cc: RunContentProcess()
  base::EnableTerminationOnOutOfMemory();

  // content/app/content_main.cc: RunContentProcess()
  base::EnableTerminationOnHeapCorruption();

  // content/app/content_main.cc: RunContentProcess()
  //   content/app/content_main_runner_impl.cc: Initialize()
  //     ReconfigureEarlyish():
  // These initializations are only relevant for PartitionAlloc-Everywhere
  // builds.
  base::allocator::PartitionAllocSupport::Get()->ReconfigureEarlyish(
      /*process_type=*/"");

  // content/app/content_main.cc: RunContentProcess()
  //   content/app/content_main_runner_impl.cc: Initialize()
  // If we are on a platform where the default allocator is overridden (e.g.
  // with PartitionAlloc on most platforms) smoke-tests that the overriding
  // logic is working correctly. If not causes a hard crash, as its unexpected
  // absence has security implications.
  CHECK(base::allocator::IsAllocatorInitialized());
}

}  // namespace naive_partition_alloc_support
