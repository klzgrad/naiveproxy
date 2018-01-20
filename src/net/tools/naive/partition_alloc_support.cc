// Copyright 2021 The Chromium Authors
// Copyright 2022 klzgrad <kizdiv@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/partition_alloc_support.h"

#include <string>

#include "base/allocator/allocator_check.h"
#include "base/allocator/buildflags.h"
#include "base/allocator/partition_alloc_features.h"
#include "base/allocator/partition_alloc_support.h"
#include "base/allocator/partition_allocator/partition_alloc_config.h"
#include "base/allocator/partition_allocator/shim/allocator_shim.h"
#include "base/allocator/partition_allocator/shim/allocator_shim_default_dispatch_to_partition_alloc.h"
#include "base/allocator/partition_allocator/thread_cache.h"
#include "base/feature_list.h"
#include "base/process/memory.h"
#include "base/time/time.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/system/sys_info.h"
#endif

#if BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
#include "base/allocator/partition_allocator/memory_reclaimer.h"
#include "base/task/single_thread_task_runner.h"
#endif

#if BUILDFLAG(IS_APPLE)
#include "base/allocator/early_zone_registration_mac.h"
#endif

namespace naive_partition_alloc_support {

void ReconfigureEarly() {
  // chrome/app/chrome_exe_main_mac.cc: main()
#if BUILDFLAG(IS_APPLE)
  partition_alloc::EarlyMallocZoneRegistration();
#endif

  // content/app/content_main.cc: ChromeMain()
#if BUILDFLAG(IS_WIN)
#if BUILDFLAG(USE_ALLOCATOR_SHIM) && BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
  // Call this early on in order to configure heap workarounds. This must be
  // called from chrome.dll. This may be a NOP on some platforms.
  allocator_shim::ConfigurePartitionAlloc();
#endif
#endif  // BUILDFLAG(IS_WIN)

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
#if BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
  allocator_shim::EnablePartitionAllocMemoryReclaimer();
#endif  // BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

  // content/app/content_main.cc: RunContentProcess()
  //   content/app/content_main_runner_impl.cc: Initialize()
  // If we are on a platform where the default allocator is overridden (e.g.
  // with PartitionAlloc on most platforms) smoke-tests that the overriding
  // logic is working correctly. If not causes a hard crash, as its unexpected
  // absence has security implications.
  CHECK(base::allocator::IsAllocatorInitialized());
}

void ReconfigureAfterFeatureListInit() {
  // TODO(bartekn): Switch to DCHECK once confirmed there are no issues.
  CHECK(base::FeatureList::GetInstance());

  // Does not use any of the security features yet.
  [[maybe_unused]] bool enable_brp = false;
  [[maybe_unused]] bool enable_brp_zapping = false;
  [[maybe_unused]] bool enable_brp_partition_memory_reclaimer = false;
  [[maybe_unused]] bool split_main_partition = false;
  [[maybe_unused]] bool use_dedicated_aligned_partition = false;
  [[maybe_unused]] bool add_dummy_ref_count = false;
  [[maybe_unused]] bool process_affected_by_brp_flag = false;

#if BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
  allocator_shim::ConfigurePartitions(
      allocator_shim::EnableBrp(enable_brp),
      allocator_shim::EnableBrpZapping(enable_brp_zapping),
      allocator_shim::EnableBrpPartitionMemoryReclaimer(
          enable_brp_partition_memory_reclaimer),
      allocator_shim::SplitMainPartition(split_main_partition),
      allocator_shim::UseDedicatedAlignedPartition(
          use_dedicated_aligned_partition),
      allocator_shim::AddDummyRefCount(add_dummy_ref_count),
      allocator_shim::AlternateBucketDistribution(
          base::features::kPartitionAllocAlternateBucketDistributionParam
              .Get()));
#endif  // BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

#if BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
  allocator_shim::internal::PartitionAllocMalloc::Allocator()
      ->EnableThreadCacheIfSupported();

  if (base::FeatureList::IsEnabled(
          base::features::kPartitionAllocLargeEmptySlotSpanRing)) {
    allocator_shim::internal::PartitionAllocMalloc::Allocator()
        ->EnableLargeEmptySlotSpanRing();
    allocator_shim::internal::PartitionAllocMalloc::AlignedAllocator()
        ->EnableLargeEmptySlotSpanRing();
  }
#endif  // BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
}

void ReconfigureAfterTaskRunnerInit() {
#if defined(PA_THREAD_CACHE_SUPPORTED) && \
    BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
  base::allocator::StartThreadCachePeriodicPurge();

#if BUILDFLAG(IS_ANDROID)
  // Lower thread cache limits to avoid stranding too much memory in the caches.
  if (base::SysInfo::IsLowEndDevice()) {
    ::partition_alloc::ThreadCacheRegistry::Instance().SetThreadCacheMultiplier(
        ::partition_alloc::ThreadCache::kDefaultMultiplier / 2.);
  }
#endif  // BUILDFLAG(IS_ANDROID)

  // Renderer processes are more performance-sensitive, increase thread cache
  // limits.
  if (/*is_performance_sensitive=*/true &&
      base::FeatureList::IsEnabled(
          base::features::kPartitionAllocLargeThreadCacheSize)) {
    size_t largest_cached_size =
        ::partition_alloc::ThreadCacheLimits::kLargeSizeThreshold;

#if BUILDFLAG(IS_ANDROID) && defined(ARCH_CPU_32_BITS)
    // Devices almost always report less physical memory than what they actually
    // have, so anything above 3GiB will catch 4GiB and above.
    if (base::SysInfo::AmountOfPhysicalMemoryMB() <= 3500)
      largest_cached_size =
          ::partition_alloc::ThreadCacheLimits::kDefaultSizeThreshold;
#endif  // BUILDFLAG(IS_ANDROID) && !defined(ARCH_CPU_64_BITS)

    ::partition_alloc::ThreadCache::SetLargestCachedSize(largest_cached_size);
  }

#endif  // defined(PA_THREAD_CACHE_SUPPORTED) &&
        // BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)

#if BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC)
  base::allocator::StartMemoryReclaimer(
      base::SingleThreadTaskRunner::GetCurrentDefault());
#endif

  if (base::FeatureList::IsEnabled(
          base::features::kPartitionAllocSortActiveSlotSpans)) {
    partition_alloc::PartitionRoot<
        partition_alloc::internal::ThreadSafe>::EnableSortActiveSlotSpans();
  }
}

}  // namespace naive_partition_alloc_support
