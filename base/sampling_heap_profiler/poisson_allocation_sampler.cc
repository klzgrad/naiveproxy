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
#include "base/macros.h"
#include "base/no_destructor.h"
#include "base/partition_alloc_buildflags.h"
#include "base/rand_util.h"
#include "base/sampling_heap_profiler/lock_free_address_hash_set.h"
#include "base/threading/thread_local_storage.h"
#include "build/build_config.h"

#if defined(OS_MACOSX)
#include <pthread.h>
#endif

namespace base {

using allocator::AllocatorDispatch;
using subtle::Atomic32;
using subtle::AtomicWord;

namespace {

#if defined(OS_MACOSX)

// On MacOS the implementation of libmalloc sometimes calls malloc recursively,
// delegating allocations between zones. That causes our hooks being called
// twice. The scoped guard allows us to detect that.
class ReentryGuard {
 public:
  ReentryGuard() : allowed_(!pthread_getspecific(entered_key_)) {
    pthread_setspecific(entered_key_, reinterpret_cast<void*>(1));
  }

  ~ReentryGuard() {
    if (LIKELY(allowed_))
      pthread_setspecific(entered_key_, reinterpret_cast<void*>(0));
  }

  operator bool() { return allowed_; }

  static void Init() {
    int result = pthread_key_create(&entered_key_, nullptr);
    DCHECK(!result);
  }

 private:
  bool allowed_;
  static pthread_key_t entered_key_;
};

pthread_key_t ReentryGuard::entered_key_;

#else

class ReentryGuard {
 public:
  operator bool() { return true; }
  static void Init() {}
};

#endif

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
  ReentryGuard guard;
  void* address = self->next->alloc_function(self->next, size, context);
  if (LIKELY(guard)) {
    PoissonAllocationSampler::RecordAlloc(
        address, size, PoissonAllocationSampler::kMalloc, nullptr);
  }
  return address;
}

void* AllocZeroInitializedFn(const AllocatorDispatch* self,
                             size_t n,
                             size_t size,
                             void* context) {
  ReentryGuard guard;
  void* address =
      self->next->alloc_zero_initialized_function(self->next, n, size, context);
  if (LIKELY(guard)) {
    PoissonAllocationSampler::RecordAlloc(
        address, n * size, PoissonAllocationSampler::kMalloc, nullptr);
  }
  return address;
}

void* AllocAlignedFn(const AllocatorDispatch* self,
                     size_t alignment,
                     size_t size,
                     void* context) {
  ReentryGuard guard;
  void* address =
      self->next->alloc_aligned_function(self->next, alignment, size, context);
  if (LIKELY(guard)) {
    PoissonAllocationSampler::RecordAlloc(
        address, size, PoissonAllocationSampler::kMalloc, nullptr);
  }
  return address;
}

void* ReallocFn(const AllocatorDispatch* self,
                void* address,
                size_t size,
                void* context) {
  ReentryGuard guard;
  // Note: size == 0 actually performs free.
  PoissonAllocationSampler::RecordFree(address);
  address = self->next->realloc_function(self->next, address, size, context);
  if (LIKELY(guard)) {
    PoissonAllocationSampler::RecordAlloc(
        address, size, PoissonAllocationSampler::kMalloc, nullptr);
  }
  return address;
}

void FreeFn(const AllocatorDispatch* self, void* address, void* context) {
  // Note: The RecordFree should be called before free_function
  // (here and in other places).
  // That is because we need to remove the recorded allocation sample before
  // free_function, as once the latter is executed the address becomes available
  // and can be allocated by another thread. That would be racy otherwise.
  PoissonAllocationSampler::RecordFree(address);
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
  ReentryGuard guard;
  unsigned num_allocated = self->next->batch_malloc_function(
      self->next, size, results, num_requested, context);
  if (LIKELY(guard)) {
    for (unsigned i = 0; i < num_allocated; ++i) {
      PoissonAllocationSampler::RecordAlloc(
          results[i], size, PoissonAllocationSampler::kMalloc, nullptr);
    }
  }
  return num_allocated;
}

void BatchFreeFn(const AllocatorDispatch* self,
                 void** to_be_freed,
                 unsigned num_to_be_freed,
                 void* context) {
  for (unsigned i = 0; i < num_to_be_freed; ++i)
    PoissonAllocationSampler::RecordFree(to_be_freed[i]);
  self->next->batch_free_function(self->next, to_be_freed, num_to_be_freed,
                                  context);
}

void FreeDefiniteSizeFn(const AllocatorDispatch* self,
                        void* address,
                        size_t size,
                        void* context) {
  PoissonAllocationSampler::RecordFree(address);
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

void PartitionAllocHook(void* address, size_t size, const char* type) {
  PoissonAllocationSampler::RecordAlloc(
      address, size, PoissonAllocationSampler::kPartitionAlloc, type);
}

void PartitionFreeHook(void* address) {
  PoissonAllocationSampler::RecordFree(address);
}

#endif  // BUILDFLAG(USE_PARTITION_ALLOC) && !defined(OS_NACL)

ThreadLocalStorage::Slot& AccumulatedBytesTLS() {
  static NoDestructor<ThreadLocalStorage::Slot> accumulated_bytes_tls;
  return *accumulated_bytes_tls;
}

}  // namespace

PoissonAllocationSampler::MuteThreadSamplesScope::MuteThreadSamplesScope() {
  CHECK(!Get()->entered_.Get());
  Get()->entered_.Set(true);
}

PoissonAllocationSampler::MuteThreadSamplesScope::~MuteThreadSamplesScope() {
  CHECK(Get()->entered_.Get());
  Get()->entered_.Set(false);
}

PoissonAllocationSampler* PoissonAllocationSampler::instance_;

PoissonAllocationSampler::PoissonAllocationSampler() {
  instance_ = this;
  auto sampled_addresses = std::make_unique<LockFreeAddressHashSet>(64);
  subtle::NoBarrier_Store(
      &g_sampled_addresses_set,
      reinterpret_cast<AtomicWord>(sampled_addresses.get()));
  sampled_addresses_stack_.push_back(std::move(sampled_addresses));
}

// static
void PoissonAllocationSampler::Init() {
  // Preallocate the TLS slot early, so it can't cause reentracy issues
  // when sampling is started.
  ignore_result(AccumulatedBytesTLS().Get());
  ReentryGuard::Init();
}

// static
void PoissonAllocationSampler::InstallAllocatorHooksOnce() {
  static bool hook_installed = InstallAllocatorHooks();
  ignore_result(hook_installed);
}

// static
bool PoissonAllocationSampler::InstallAllocatorHooks() {
#if BUILDFLAG(USE_ALLOCATOR_SHIM)
  allocator::InsertAllocatorDispatch(&g_allocator_dispatch);
#else
  ignore_result(g_allocator_dispatch);
  DLOG(WARNING)
      << "base::allocator shims are not available for memory sampling.";
#endif  // BUILDFLAG(USE_ALLOCATOR_SHIM)

#if BUILDFLAG(USE_PARTITION_ALLOC) && !defined(OS_NACL)
  PartitionAllocHooks::SetAllocationHook(&PartitionAllocHook);
  PartitionAllocHooks::SetFreeHook(&PartitionFreeHook);
#endif  // BUILDFLAG(USE_PARTITION_ALLOC) && !defined(OS_NACL)

  int32_t hooks_install_callback_has_been_set =
      subtle::Acquire_CompareAndSwap(&g_hooks_installed, 0, 1);
  if (hooks_install_callback_has_been_set)
    g_hooks_install_callback();

  return true;
}

// static
void PoissonAllocationSampler::SetHooksInstallCallback(
    void (*hooks_install_callback)()) {
  CHECK(!g_hooks_install_callback && hooks_install_callback);
  g_hooks_install_callback = hooks_install_callback;

  int32_t profiler_has_already_been_initialized =
      subtle::Release_CompareAndSwap(&g_hooks_installed, 0, 1);
  if (profiler_has_already_been_initialized)
    g_hooks_install_callback();
}

void PoissonAllocationSampler::Start() {
  InstallAllocatorHooksOnce();
  subtle::Barrier_AtomicIncrement(&g_running, 1);
}

void PoissonAllocationSampler::Stop() {
  AtomicWord count = subtle::Barrier_AtomicIncrement(&g_running, -1);
  CHECK_GE(count, 0);
}

void PoissonAllocationSampler::SetSamplingInterval(size_t sampling_interval) {
  // TODO(alph): Reset the sample being collected if running.
  subtle::Release_Store(&g_sampling_interval,
                        static_cast<AtomicWord>(sampling_interval));
}

// static
size_t PoissonAllocationSampler::GetNextSampleInterval(size_t interval) {
  if (UNLIKELY(g_deterministic))
    return interval;

  // We sample with a Poisson process, with constant average sampling
  // interval. This follows the exponential probability distribution with
  // parameter λ = 1/interval where |interval| is the average number of bytes
  // between samples.
  // Let u be a uniformly distributed random number between 0 and 1, then
  // next_sample = -ln(u) / λ
  double uniform = RandDouble();
  double value = -log(uniform) * interval;
  size_t min_value = sizeof(intptr_t);
  // We limit the upper bound of a sample interval to make sure we don't have
  // huge gaps in the sampling stream. Probability of the upper bound gets hit
  // is exp(-20) ~ 2e-9, so it should not skew the distribution.
  size_t max_value = interval * 20;
  if (UNLIKELY(value < min_value))
    return min_value;
  if (UNLIKELY(value > max_value))
    return max_value;
  return static_cast<size_t>(value);
}

// static
void PoissonAllocationSampler::RecordAlloc(void* address,
                                           size_t size,
                                           AllocatorType type,
                                           const char* context) {
  if (UNLIKELY(!subtle::NoBarrier_Load(&g_running)))
    return;
  if (UNLIKELY(ThreadLocalStorage::HasBeenDestroyed()))
    return;

  intptr_t accumulated_bytes =
      reinterpret_cast<intptr_t>(AccumulatedBytesTLS().Get());
  accumulated_bytes += size;
  if (LIKELY(accumulated_bytes < 0)) {
    AccumulatedBytesTLS().Set(reinterpret_cast<void*>(accumulated_bytes));
    return;
  }

  size_t mean_interval = subtle::NoBarrier_Load(&g_sampling_interval);
  size_t samples = accumulated_bytes / mean_interval;
  accumulated_bytes %= mean_interval;

  do {
    accumulated_bytes -= GetNextSampleInterval(mean_interval);
    ++samples;
  } while (accumulated_bytes >= 0);

  AccumulatedBytesTLS().Set(reinterpret_cast<void*>(accumulated_bytes));

  instance_->DoRecordAlloc(samples * mean_interval, size, address, type,
                           context);
}

void PoissonAllocationSampler::DoRecordAlloc(size_t total_allocated,
                                             size_t size,
                                             void* address,
                                             AllocatorType type,
                                             const char* context) {
  if (entered_.Get())
    return;
  MuteThreadSamplesScope no_reentrancy_scope;
  AutoLock lock(mutex_);
  // TODO(alph): Sometimes RecordAlloc is called twice in a row without
  // a RecordFree in between. Investigate it.
  if (!sampled_addresses_set().Contains(address)) {
    sampled_addresses_set().Insert(address);
    BalanceAddressesHashSet();
    for (auto* observer : observers_)
      observer->SampleAdded(address, size, total_allocated, type, context);
  }
}

// static
void PoissonAllocationSampler::RecordFree(void* address) {
  if (UNLIKELY(address == nullptr))
    return;
  if (UNLIKELY(sampled_addresses_set().Contains(address)))
    instance_->DoRecordFree(address);
}

void PoissonAllocationSampler::DoRecordFree(void* address) {
  if (UNLIKELY(ThreadLocalStorage::HasBeenDestroyed()))
    return;
  if (entered_.Get())
    return;
  MuteThreadSamplesScope no_reentrancy_scope;
  AutoLock lock(mutex_);
  for (auto* observer : observers_)
    observer->SampleRemoved(address);
  sampled_addresses_set().Remove(address);
}

void PoissonAllocationSampler::BalanceAddressesHashSet() {
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
  subtle::Release_Store(&g_sampled_addresses_set,
                        reinterpret_cast<AtomicWord>(new_set.get()));
  // We still have to keep all the old maps alive to resolve the theoretical
  // race with readers in |RecordFree| that have already obtained the map,
  // but haven't yet managed to access it.
  sampled_addresses_stack_.push_back(std::move(new_set));
}

// static
LockFreeAddressHashSet& PoissonAllocationSampler::sampled_addresses_set() {
  return *reinterpret_cast<LockFreeAddressHashSet*>(
      subtle::NoBarrier_Load(&g_sampled_addresses_set));
}

// static
PoissonAllocationSampler* PoissonAllocationSampler::Get() {
  static NoDestructor<PoissonAllocationSampler> instance;
  return instance.get();
}

// static
void PoissonAllocationSampler::SuppressRandomnessForTest(bool suppress) {
  g_deterministic = suppress;
}

void PoissonAllocationSampler::AddSamplesObserver(SamplesObserver* observer) {
  MuteThreadSamplesScope no_reentrancy_scope;
  AutoLock lock(mutex_);
  observers_.push_back(observer);
}

void PoissonAllocationSampler::RemoveSamplesObserver(
    SamplesObserver* observer) {
  MuteThreadSamplesScope no_reentrancy_scope;
  AutoLock lock(mutex_);
  auto it = std::find(observers_.begin(), observers_.end(), observer);
  CHECK(it != observers_.end());
  observers_.erase(it);
}

}  // namespace base
