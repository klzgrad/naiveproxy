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
#include "base/threading/thread_local_storage.h"
#include "build/build_config.h"

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

// Pointer to the current |SamplingHeapProfiler::SamplesMap|.
AtomicWord g_current_samples_map;

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
  auto samples_map = std::make_unique<SamplesMap>(64);
  base::subtle::NoBarrier_Store(
      &g_current_samples_map, reinterpret_cast<AtomicWord>(samples_map.get()));
  sample_maps_.push(std::move(samples_map));
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
  // TODO(alph): Consider using debug::TraceStackFramePointers. It should be
  // somewhat faster than base::debug::StackTrace.
  base::debug::StackTrace trace;
  size_t count;
  void* const* addresses = const_cast<void* const*>(trace.Addresses(&count));
  const uint32_t kSkipProfilerOwnFrames = 2;
  skip_frames += kSkipProfilerOwnFrames;
  sample->stack.insert(
      sample->stack.end(), &addresses[skip_frames],
      &addresses[std::max(count, static_cast<size_t>(skip_frames))]);
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
    EnsureNoRehashingMap().emplace(address, std::move(sample));
  }
  entered_.Set(false);
}

// static
void SamplingHeapProfiler::RecordFree(void* address) {
  const SamplesMap& samples = SamplingHeapProfiler::samples();
  if (UNLIKELY(samples.find(address) != samples.end()))
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
    SamplesMap& samples = this->samples();
    auto it = samples.find(address);
    CHECK(it != samples.end());
    for (auto* observer : observers_)
      observer->SampleRemoved(it->second.ordinal);
    samples.erase(it);
  }
  entered_.Set(false);
}

SamplingHeapProfiler::SamplesMap& SamplingHeapProfiler::EnsureNoRehashingMap() {
  // The function makes sure we never rehash the current map in place.
  // Instead if it comes close to the rehashing boundary, we allocate a twice
  // larger map, copy the samples into it, and atomically switch new readers
  // to use the new map.
  // We still have to keep all the old maps alive to resolve the theoretical
  // race with readers in |RecordFree| that have already obtained the map,
  // but haven't yet managed to access it.
  SamplesMap& samples = this->samples();
  size_t max_items_before_rehash =
      static_cast<size_t>(samples.bucket_count() * samples.max_load_factor());
  // Conservatively use 2 instead of 1 to workaround potential rounding errors.
  bool may_rehash_on_insert = samples.size() + 2 >= max_items_before_rehash;
  if (!may_rehash_on_insert)
    return samples;
  auto new_map = std::make_unique<SamplesMap>(samples.begin(), samples.end(),
                                              samples.bucket_count() * 2);
  base::subtle::Release_Store(&g_current_samples_map,
                              reinterpret_cast<AtomicWord>(new_map.get()));
  sample_maps_.push(std::move(new_map));
  return this->samples();
}

// static
SamplingHeapProfiler::SamplesMap& SamplingHeapProfiler::samples() {
  return *reinterpret_cast<SamplesMap*>(
      base::subtle::NoBarrier_Load(&g_current_samples_map));
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
    for (auto& it : this->samples()) {
      Sample& sample = it.second;
      if (sample.ordinal > profile_id)
        samples.push_back(sample);
    }
  }
  entered_.Set(false);
  return samples;
}

}  // namespace base
