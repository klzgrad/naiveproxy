// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_SAMPLING_HEAP_PROFILER_SAMPLING_HEAP_PROFILER_H_
#define BASE_SAMPLING_HEAP_PROFILER_SAMPLING_HEAP_PROFILER_H_

#include <unordered_map>
#include <vector>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/sampling_heap_profiler/poisson_allocation_sampler.h"
#include "base/synchronization/lock.h"

namespace base {

template <typename T>
class NoDestructor;

// The class implements sampling profiling of native memory heap.
// It uses PoissonAllocationSampler to aggregate the heap allocations and
// record samples.
// The recorded samples can then be retrieved using GetSamples method.
class BASE_EXPORT SamplingHeapProfiler
    : private PoissonAllocationSampler::SamplesObserver {
 public:
  class BASE_EXPORT Sample {
   public:
    Sample(const Sample&);
    ~Sample();

    size_t size;   // Allocation size.
    size_t total;  // Total size attributed to the sample.
    std::vector<void*> stack;

   private:
    friend class SamplingHeapProfiler;

    Sample(size_t, size_t total, uint32_t ordinal);

    uint32_t ordinal;
  };

  uint32_t Start();
  void Stop();
  void SetSamplingInterval(size_t sampling_interval);

  std::vector<Sample> GetSamples(uint32_t profile_id);

  static void Init();
  static SamplingHeapProfiler* Get();

 private:
  SamplingHeapProfiler();
  ~SamplingHeapProfiler() override;

  // PoissonAllocationSampler::SamplesObserver
  void SampleAdded(void* address,
                   size_t size,
                   size_t total,
                   PoissonAllocationSampler::AllocatorType type,
                   const char* context) override;
  void SampleRemoved(void* address) override;

  Lock mutex_;
  std::unordered_map<void*, Sample> samples_;
  uint32_t last_sample_ordinal_ = 1;

  friend class NoDestructor<SamplingHeapProfiler>;

  DISALLOW_COPY_AND_ASSIGN(SamplingHeapProfiler);
};

}  // namespace base

#endif  // BASE_SAMPLING_HEAP_PROFILER_SAMPLING_HEAP_PROFILER_H_
