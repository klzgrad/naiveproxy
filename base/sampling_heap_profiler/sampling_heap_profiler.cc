// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sampling_heap_profiler/sampling_heap_profiler.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/allocator/allocator_shim.h"
#include "base/allocator/buildflags.h"
#include "base/allocator/partition_allocator/partition_alloc.h"
#include "base/atomicops.h"
#include "base/debug/stack_trace.h"
#include "base/macros.h"
#include "base/no_destructor.h"
#include "base/partition_alloc_buildflags.h"
#include "base/rand_util.h"
#include "base/sampling_heap_profiler/lock_free_address_hash_set.h"
#include "base/threading/thread_local_storage.h"
#include "build/build_config.h"

#if defined(OS_ANDROID) && BUILDFLAG(CAN_UNWIND_WITH_CFI_TABLE) && \
    defined(OFFICIAL_BUILD)
#include "base/trace_event/cfi_backtrace_android.h"
#endif

namespace base {

using base::allocator::AllocatorDispatch;
using base::subtle::Atomic32;
using base::subtle::AtomicWord;

namespace {

// Control how many top frames to skip when recording call stack.
// These frames correspond to the profiler own frames.
const uint32_t kSkipBaseAllocatorFrames = 2;

const size_t kDefaultSamplingIntervalBytes = 128 * 1024;

// Controls if sample intervals should not be randomized. Used for testing.
bool g_deterministic;

// A positive value if profiling is running, otherwise it's zero.
Atomic32 g_running;

// Pointer to the current |LockFreeAddressHashSet|.
AtomicWord g_sampled_addresses_set;

// Sampling interval parameter, the mean value for intervals between samples.
AtomicWord g_sampling_interval = kDefaultSamplingIntervalBytes;

void (*g_hooks_install_callback)();
Atomic32 g_hooks_installed;

void* AllocFn(const AllocatorDispatch* self, size_t size, void* context) {
  void* address = self->next->alloc_function(self->next, size, context);
  SamplingHeapProfiler::RecordAlloc(address, size, kSkipBaseAllocatorFrames);
  return address;
}

void* AllocZeroInitializedFn(const AllocatorDispatch* self,
                             size_t n,
                             size_t size,
                             void* context) {
  void* address =
      self->next->alloc_zero_initialized_function(self->next, n, size, context);
  SamplingHeapProfiler::RecordAlloc(address, n * size,
                                    kSkipBaseAllocatorFrames);
  return address;
}

void* AllocAlignedFn(const AllocatorDispatch* self,
                     size_t alignment,
                     size_t size,
                     void* context) {
  void* address =
      self->next->alloc_aligned_function(self->next, alignment, size, context);
  SamplingHeapProfiler::RecordAlloc(address, size, kSkipBaseAllocatorFrames);
  return address;
}

void* ReallocFn(const AllocatorDispatch* self,
                void* address,
                size_t size,
                void* context) {
  // Note: size == 0 actually performs free.
  SamplingHeapProfiler::RecordFree(address);
  address = self->next->realloc_function(self->next, address, size, context);
  SamplingHeapProfiler::RecordAlloc(address, size, kSkipBaseAllocatorFrames);
  return address;
}

void FreeFn(const AllocatorDispatch* self, void* address, void* context) {
  SamplingHeapProfiler::RecordFree(address);
  self->next->free_function(self->next, address, context);
}

size_t GetSizeEstimateFn(const AllocatorDispatch* self,
                         void* address,
                         void* context) {
  return self->next->get_size_estimate_function(self->next, address, context);
}

unsigned BatchMallocFn(const AllocatorDispatch* self,
                       size_t size,
                       void** results,
                       unsigned num_requested,
                       void* context) {
  unsigned num_allocated = self->next->batch_malloc_function(
      self->next, size, results, num_requested, context);
  for (unsigned i = 0; i < num_allocated; ++i) {
    SamplingHeapProfiler::RecordAlloc(results[i], size,
                                      kSkipBaseAllocatorFrames);
  }
  return num_allocated;
}

void BatchFreeFn(const AllocatorDispatch* self,
                 void** to_be_freed,
                 unsigned num_to_be_freed,
                 void* context) {
  for (unsigned i = 0; i < num_to_be_freed; ++i)
    SamplingHeapProfiler::RecordFree(to_be_freed[i]);
  self->next->batch_free_function(self->next, to_be_freed, num_to_be_freed,
                                  context);
}

void FreeDefiniteSizeFn(const AllocatorDispatch* self,
                        void* address,
                        size_t size,
                        void* context) {
  SamplingHeapProfiler::RecordFree(address);
  self->next->free_definite_size_function(self->next, address, size, context);
}

AllocatorDispatch g_allocator_dispatch = {&AllocFn,
                                          &AllocZeroInitializedFn,
                                          &AllocAlignedFn,
                                          &ReallocFn,
                                          &FreeFn,
                                          &GetSizeEstimateFn,
                                          &BatchMallocFn,
                                          &BatchFreeFn,
                                          &FreeDefiniteSizeFn,
                                          nullptr};

#if BUILDFLAG(USE_PARTITION_ALLOC) && !defined(OS_NACL)

void PartitionAllocHook(void* address, size_t size, const char*) {
  SamplingHeapProfiler::RecordAlloc(address, size);
}

void PartitionFreeHook(void* address) {
  SamplingHeapProfiler::RecordFree(address);
}

#endif  // BUILDFLAG(USE_PARTITION_ALLOC) && !defined(OS_NACL)

ThreadLocalStorage::Slot& AccumulatedBytesTLS() {
  static base::NoDestructor<base::ThreadLocalStorage::Slot>
      accumulated_bytes_tls;
  return *accumulated_bytes_tls;
}

}  // namespace

SamplingHeapProfiler::Sample::Sample(size_t size,
                                     size_t total,
                                     uint32_t ordinal)
    : size(size), total(total), ordinal(ordinal) {}

SamplingHeapProfiler::Sample::Sample(const Sample&) = default;

SamplingHeapProfiler::Sample::~Sample() = default;

SamplingHeapProfiler* SamplingHeapProfiler::instance_;

SamplingHeapProfiler::SamplingHeapProfiler() {
  instance_ = this;
  auto sampled_addresses = std::make_unique<LockFreeAddressHashSet>(64);
  base::subtle::NoBarrier_Store(
      &g_sampled_addresses_set,
      reinterpret_cast<AtomicWord>(sampled_addresses.get()));
  sampled_addresses_stack_.push(std::move(sampled_addresses));
}

// static
void SamplingHeapProfiler::InitTLSSlot() {
  // Preallocate the TLS slot early, so it can't cause reentracy issues
  // when sampling is started.
  ignore_result(AccumulatedBytesTLS().Get());
}

// static
void SamplingHeapProfiler::InstallAllocatorHooksOnce() {
  static bool hook_installed = InstallAllocatorHooks();
  ignore_result(hook_installed);
}

// static
bool SamplingHeapProfiler::InstallAllocatorHooks() {
#if BUILDFLAG(USE_ALLOCATOR_SHIM)
  base::allocator::InsertAllocatorDispatch(&g_allocator_dispatch);
#else
  ignore_result(g_allocator_dispatch);
  DLOG(WARNING)
      << "base::allocator shims are not available for memory sampling.";
#endif  // BUILDFLAG(USE_ALLOCATOR_SHIM)

#if BUILDFLAG(USE_PARTITION_ALLOC) && !defined(OS_NACL)
  base::PartitionAllocHooks::SetAllocationHook(&PartitionAllocHook);
  base::PartitionAllocHooks::SetFreeHook(&PartitionFreeHook);
#endif  // BUILDFLAG(USE_PARTITION_ALLOC) && !defined(OS_NACL)

  int32_t hooks_install_callback_has_been_set =
      base::subtle::Acquire_CompareAndSwap(&g_hooks_installed, 0, 1);
  if (hooks_install_callback_has_been_set)
    g_hooks_install_callback();

  return true;
}

// static
void SamplingHeapProfiler::SetHooksInstallCallback(
    void (*hooks_install_callback)()) {
  CHECK(!g_hooks_install_callback && hooks_install_callback);
  g_hooks_install_callback = hooks_install_callback;

  int32_t profiler_has_already_been_initialized =
      base::subtle::Release_CompareAndSwap(&g_hooks_installed, 0, 1);
  if (profiler_has_already_been_initialized)
    g_hooks_install_callback();
}

uint32_t SamplingHeapProfiler::Start() {
#if defined(OS_ANDROID) && BUILDFLAG(CAN_UNWIND_WITH_CFI_TABLE) && \
    defined(OFFICIAL_BUILD)
  if (!base::trace_event::CFIBacktraceAndroid::GetInitializedInstance()
           ->can_unwind_stack_frames()) {
    LOG(WARNING) << "Sampling heap profiler: Stack unwinding is not available.";
    return 0;
  }
#endif
  InstallAllocatorHooksOnce();
  base::subtle::Barrier_AtomicIncrement(&g_running, 1);
  return last_sample_ordinal_;
}

void SamplingHeapProfiler::Stop() {
  AtomicWord count = base::subtle::Barrier_AtomicIncrement(&g_running, -1);
  CHECK_GE(count, 0);
}

void SamplingHeapProfiler::SetSamplingInterval(size_t sampling_interval) {
  // TODO(alph): Reset the sample being collected if running.
  base::subtle::Release_Store(&g_sampling_interval,
                              static_cast<AtomicWord>(sampling_interval));
}

// static
size_t SamplingHeapProfiler::GetNextSampleInterval(size_t interval) {
  if (UNLIKELY(g_deterministic))
    return interval;

  // We sample with a Poisson process, with constant average sampling
  // interval. This follows the exponential probability distribution with
  // parameter λ = 1/interval where |interval| is the average number of bytes
  // between samples.
  // Let u be a uniformly distributed random number between 0 and 1, then
  // next_sample = -ln(u) / λ
  double uniform = base::RandDouble();
  double value = -log(uniform) * interval;
  size_t min_value = sizeof(intptr_t);
  // We limit the upper bound of a sample interval to make sure we don't have
  // huge gaps in the sampling stream. Probability of the upper bound gets hit
  // is exp(-20) ~ 2e-9, so it should not skew the distibution.
  size_t max_value = interval * 20;
  if (UNLIKELY(value < min_value))
    return min_value;
  if (UNLIKELY(value > max_value))
    return max_value;
  return static_cast<size_t>(value);
}

// static
void SamplingHeapProfiler::RecordAlloc(void* address,
                                       size_t size,
                                       uint32_t skip_frames) {
  if (UNLIKELY(!base::subtle::NoBarrier_Load(&g_running)))
    return;
  if (UNLIKELY(base::ThreadLocalStorage::HasBeenDestroyed()))
    return;

  // TODO(alph): On MacOS it may call the hook several times for a single
  // allocation. Handle the case.

  intptr_t accumulated_bytes =
      reinterpret_cast<intptr_t>(AccumulatedBytesTLS().Get());
  accumulated_bytes += size;
  if (LIKELY(accumulated_bytes < 0)) {
    AccumulatedBytesTLS().Set(reinterpret_cast<void*>(accumulated_bytes));
    return;
  }

  size_t mean_interval = base::subtle::NoBarrier_Load(&g_sampling_interval);
  size_t samples = accumulated_bytes / mean_interval;
  accumulated_bytes %= mean_interval;

  do {
    accumulated_bytes -= GetNextSampleInterval(mean_interval);
    ++samples;
  } while (accumulated_bytes >= 0);

  AccumulatedBytesTLS().Set(reinterpret_cast<void*>(accumulated_bytes));

  instance_->DoRecordAlloc(samples * mean_interval, size, address, skip_frames);
}

void SamplingHeapProfiler::RecordStackTrace(Sample* sample,
                                            uint32_t skip_frames) {
#if !defined(OS_NACL)
  constexpr uint32_t kMaxStackEntries = 256;
  constexpr uint32_t kSkipProfilerOwnFrames = 2;
  skip_frames += kSkipProfilerOwnFrames;
#if defined(OS_ANDROID) && BUILDFLAG(CAN_UNWIND_WITH_CFI_TABLE) && \
    defined(OFFICIAL_BUILD)
  const void* frames[kMaxStackEntries];
  size_t frame_count =
      base::trace_event::CFIBacktraceAndroid::GetInitializedInstance()->Unwind(
          frames, kMaxStackEntries);
#elif BUILDFLAG(CAN_UNWIND_WITH_FRAME_POINTERS)
  const void* frames[kMaxStackEntries];
  size_t frame_count = base::debug::TraceStackFramePointers(
      frames, kMaxStackEntries, skip_frames);
  skip_frames = 0;
#else
  // Fall-back to capturing the stack with base::debug::StackTrace,
  // which is likely slower, but more reliable.
  base::debug::StackTrace stack_trace(kMaxStackEntries);
  size_t frame_count = 0;
  const void* const* frames = stack_trace.Addresses(&frame_count);
#endif

  sample->stack.insert(
      sample->stack.end(), const_cast<void**>(&frames[skip_frames]),
      const_cast<void**>(&frames[std::max<size_t>(frame_count, skip_frames)]));
#endif
}

void SamplingHeapProfiler::DoRecordAlloc(size_t total_allocated,
                                         size_t size,
                                         void* address,
                                         uint32_t skip_frames) {
  if (entered_.Get())
    return;
  entered_.Set(true);
  {
    base::AutoLock lock(mutex_);
    Sample sample(size, total_allocated, ++last_sample_ordinal_);
    RecordStackTrace(&sample, skip_frames);
    for (auto* observer : observers_)
      observer->SampleAdded(sample.ordinal, size, total_allocated);
    samples_.emplace(address, std::move(sample));
    // TODO(alph): Sometimes RecordAlloc is called twice in a row without
    // a RecordFree in between. Investigate it.
    if (!sampled_addresses_set().Contains(address))
      sampled_addresses_set().Insert(address);
    BalanceAddressesHashSet();
  }
  entered_.Set(false);
}

// static
void SamplingHeapProfiler::RecordFree(void* address) {
  if (UNLIKELY(address == nullptr))
    return;
  if (UNLIKELY(sampled_addresses_set().Contains(address)))
    instance_->DoRecordFree(address);
}

void SamplingHeapProfiler::DoRecordFree(void* address) {
  if (UNLIKELY(base::ThreadLocalStorage::HasBeenDestroyed()))
    return;
  if (entered_.Get())
    return;
  entered_.Set(true);
  {
    base::AutoLock lock(mutex_);
    auto it = samples_.find(address);
    CHECK(it != samples_.end());
    for (auto* observer : observers_)
      observer->SampleRemoved(it->second.ordinal);
    samples_.erase(it);
    sampled_addresses_set().Remove(address);
  }
  entered_.Set(false);
}

void SamplingHeapProfiler::BalanceAddressesHashSet() {
  // Check if the load_factor of the current addresses hash set becomes higher
  // than 1, allocate a new twice larger one, copy all the data,
  // and switch to using it.
  // During the copy process no other writes are made to both sets
  // as it's behind the lock.
  // All the readers continue to use the old one until the atomic switch
  // process takes place.
  LockFreeAddressHashSet& current_set = sampled_addresses_set();
  if (current_set.load_factor() < 1)
    return;
  auto new_set =
      std::make_unique<LockFreeAddressHashSet>(current_set.buckets_count() * 2);
  new_set->Copy(current_set);
  // Atomically switch all the new readers to the new set.
  base::subtle::Release_Store(&g_sampled_addresses_set,
                              reinterpret_cast<AtomicWord>(new_set.get()));
  // We still have to keep all the old maps alive to resolve the theoretical
  // race with readers in |RecordFree| that have already obtained the map,
  // but haven't yet managed to access it.
  sampled_addresses_stack_.push(std::move(new_set));
}

// static
LockFreeAddressHashSet& SamplingHeapProfiler::sampled_addresses_set() {
  return *reinterpret_cast<LockFreeAddressHashSet*>(
      base::subtle::NoBarrier_Load(&g_sampled_addresses_set));
}

// static
SamplingHeapProfiler* SamplingHeapProfiler::GetInstance() {
  static base::NoDestructor<SamplingHeapProfiler> instance;
  return instance.get();
}

// static
void SamplingHeapProfiler::SuppressRandomnessForTest(bool suppress) {
  g_deterministic = suppress;
}

void SamplingHeapProfiler::AddSamplesObserver(SamplesObserver* observer) {
  CHECK(!entered_.Get());
  entered_.Set(true);
  {
    base::AutoLock lock(mutex_);
    observers_.push_back(observer);
  }
  entered_.Set(false);
}

void SamplingHeapProfiler::RemoveSamplesObserver(SamplesObserver* observer) {
  CHECK(!entered_.Get());
  entered_.Set(true);
  {
    base::AutoLock lock(mutex_);
    auto it = std::find(observers_.begin(), observers_.end(), observer);
    CHECK(it != observers_.end());
    observers_.erase(it);
  }
  entered_.Set(false);
}

std::vector<SamplingHeapProfiler::Sample> SamplingHeapProfiler::GetSamples(
    uint32_t profile_id) {
  CHECK(!entered_.Get());
  entered_.Set(true);
  std::vector<Sample> samples;
  {
    base::AutoLock lock(mutex_);
    for (auto& it : samples_) {
      Sample& sample = it.second;
      if (sample.ordinal > profile_id)
        samples.push_back(sample);
    }
  }
  entered_.Set(false);
  return samples;
}

}  // namespace base
