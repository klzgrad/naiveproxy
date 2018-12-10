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
#include "base/sampling_heap_profiler/lock_free_address_hash_set.h"
#include "base/threading/thread_local_storage.h"
#include "build/build_config.h"

#if defined(OS_ANDROID) && BUILDFLAG(CAN_UNWIND_WITH_CFI_TABLE) && \
    defined(OFFICIAL_BUILD)
#include "base/trace_event/cfi_backtrace_android.h"
#endif

namespace base {

SamplingHeapProfiler::Sample::Sample(size_t size,
                                     size_t total,
                                     uint32_t ordinal)
    : size(size), total(total), ordinal(ordinal) {}

SamplingHeapProfiler::Sample::Sample(const Sample&) = default;
SamplingHeapProfiler::Sample::~Sample() = default;

SamplingHeapProfiler::SamplingHeapProfiler() = default;
SamplingHeapProfiler::~SamplingHeapProfiler() = default;

uint32_t SamplingHeapProfiler::Start() {
#if defined(OS_ANDROID) && BUILDFLAG(CAN_UNWIND_WITH_CFI_TABLE) && \
    defined(OFFICIAL_BUILD)
  if (!trace_event::CFIBacktraceAndroid::GetInitializedInstance()
           ->can_unwind_stack_frames()) {
    LOG(WARNING) << "Sampling heap profiler: Stack unwinding is not available.";
    return 0;
  }
#endif
  auto* sampler = PoissonAllocationSampler::Get();
  sampler->AddSamplesObserver(this);
  sampler->Start();
  return last_sample_ordinal_;
}

void SamplingHeapProfiler::Stop() {
  auto* sampler = PoissonAllocationSampler::Get();
  sampler->Stop();
  sampler->RemoveSamplesObserver(this);
}

void SamplingHeapProfiler::SetSamplingInterval(size_t sampling_interval) {
  PoissonAllocationSampler::Get()->SetSamplingInterval(sampling_interval);
}

namespace {
void RecordStackTrace(SamplingHeapProfiler::Sample* sample) {
#if !defined(OS_NACL)
  constexpr uint32_t kMaxStackEntries = 256;
  constexpr uint32_t kSkipProfilerOwnFrames = 2;
  uint32_t skip_frames = kSkipProfilerOwnFrames;
#if defined(OS_ANDROID) && BUILDFLAG(CAN_UNWIND_WITH_CFI_TABLE) && \
    defined(OFFICIAL_BUILD)
  const void* frames[kMaxStackEntries];
  size_t frame_count =
      trace_event::CFIBacktraceAndroid::GetInitializedInstance()->Unwind(
          frames, kMaxStackEntries);
#elif BUILDFLAG(CAN_UNWIND_WITH_FRAME_POINTERS)
  const void* frames[kMaxStackEntries];
  size_t frame_count =
      debug::TraceStackFramePointers(frames, kMaxStackEntries, skip_frames);
  skip_frames = 0;
#else
  // Fall-back to capturing the stack with debug::StackTrace,
  // which is likely slower, but more reliable.
  debug::StackTrace stack_trace(kMaxStackEntries);
  size_t frame_count = 0;
  const void* const* frames = stack_trace.Addresses(&frame_count);
#endif

  sample->stack.insert(
      sample->stack.end(), const_cast<void**>(&frames[skip_frames]),
      const_cast<void**>(&frames[std::max<size_t>(frame_count, skip_frames)]));
#endif
}
}  // namespace

void SamplingHeapProfiler::SampleAdded(void* address,
                                       size_t size,
                                       size_t total,
                                       PoissonAllocationSampler::AllocatorType,
                                       const char*) {
  AutoLock lock(mutex_);
  Sample sample(size, total, ++last_sample_ordinal_);
  RecordStackTrace(&sample);
  samples_.emplace(address, std::move(sample));
}

void SamplingHeapProfiler::SampleRemoved(void* address) {
  AutoLock lock(mutex_);
  auto it = samples_.find(address);
  if (it != samples_.end())
    samples_.erase(it);
}

std::vector<SamplingHeapProfiler::Sample> SamplingHeapProfiler::GetSamples(
    uint32_t profile_id) {
  // Make sure the sampler does not invoke |SampleAdded| or |SampleRemoved|
  // on this thread. Otherwise it could have end up with a deadlock.
  // See crbug.com/882495
  PoissonAllocationSampler::MuteThreadSamplesScope no_samples_scope;
  AutoLock lock(mutex_);
  std::vector<Sample> samples;
  samples.reserve(samples_.size());
  for (auto& it : samples_) {
    Sample& sample = it.second;
    if (sample.ordinal > profile_id)
      samples.push_back(sample);
  }
  return samples;
}

// static
void SamplingHeapProfiler::Init() {
  PoissonAllocationSampler::Init();
}

// static
SamplingHeapProfiler* SamplingHeapProfiler::Get() {
  static NoDestructor<SamplingHeapProfiler> instance;
  return instance.get();
}

}  // namespace base
