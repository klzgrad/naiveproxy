// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sampling_heap_profiler/sampling_heap_profiler.h"

#include <stdlib.h>
#include <cinttypes>

#include "base/allocator/allocator_shim.h"
#include "base/debug/alias.h"
#include "base/threading/simple_thread.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

class SamplingHeapProfilerTest : public ::testing::Test {
 public:
  void SetUp() override {
#if defined(OS_MACOSX)
    allocator::InitializeAllocatorShim();
#endif
    SamplingHeapProfiler::Init();
  }

  size_t GetNextSample(size_t mean_interval) {
    return PoissonAllocationSampler::GetNextSampleInterval(mean_interval);
  }
};

class SamplesCollector : public PoissonAllocationSampler::SamplesObserver {
 public:
  explicit SamplesCollector(size_t watch_size) : watch_size_(watch_size) {}

  void SampleAdded(void* address,
                   size_t size,
                   size_t,
                   PoissonAllocationSampler::AllocatorType,
                   const char*) override {
    if (sample_added || size != watch_size_)
      return;
    sample_address_ = address;
    sample_added = true;
  }

  void SampleRemoved(void* address) override {
    if (address == sample_address_)
      sample_removed = true;
  }

  bool sample_added = false;
  bool sample_removed = false;

 private:
  size_t watch_size_;
  void* sample_address_ = nullptr;
};

TEST_F(SamplingHeapProfilerTest, SampleObserver) {
  SamplesCollector collector(10000);
  auto* sampler = PoissonAllocationSampler::Get();
  sampler->SuppressRandomnessForTest(true);
  sampler->SetSamplingInterval(1024);
  sampler->Start();
  sampler->AddSamplesObserver(&collector);
  void* volatile p = malloc(10000);
  free(p);
  sampler->Stop();
  sampler->RemoveSamplesObserver(&collector);
  EXPECT_TRUE(collector.sample_added);
  EXPECT_TRUE(collector.sample_removed);
}

TEST_F(SamplingHeapProfilerTest, SampleObserverMuted) {
  SamplesCollector collector(10000);
  auto* sampler = PoissonAllocationSampler::Get();
  sampler->SuppressRandomnessForTest(true);
  sampler->SetSamplingInterval(1024);
  sampler->Start();
  sampler->AddSamplesObserver(&collector);
  {
    PoissonAllocationSampler::ScopedMuteThreadSamples muted_scope;
    void* volatile p = malloc(10000);
    free(p);
  }
  sampler->Stop();
  sampler->RemoveSamplesObserver(&collector);
  EXPECT_FALSE(collector.sample_added);
  EXPECT_FALSE(collector.sample_removed);
}

TEST_F(SamplingHeapProfilerTest, IntervalRandomizationSanity) {
  PoissonAllocationSampler::Get()->SuppressRandomnessForTest(false);
  constexpr int iterations = 50;
  constexpr size_t target = 10000000;
  int sum = 0;
  for (int i = 0; i < iterations; ++i) {
    int samples = 0;
    for (size_t value = 0; value < target; value += GetNextSample(10000))
      ++samples;
    // There are should be ~ target/10000 = 1000 samples.
    sum += samples;
  }
  int mean_samples = sum / iterations;
  EXPECT_NEAR(1000, mean_samples, 100);  // 10% tolerance.
}

const int kNumberOfAllocations = 10000;

NOINLINE void Allocate1() {
  void* p = malloc(400);
  base::debug::Alias(&p);
}

NOINLINE void Allocate2() {
  void* p = malloc(700);
  base::debug::Alias(&p);
}

NOINLINE void Allocate3() {
  void* p = malloc(20480);
  base::debug::Alias(&p);
}

class MyThread1 : public SimpleThread {
 public:
  MyThread1() : SimpleThread("MyThread1") {}
  void Run() override {
    for (int i = 0; i < kNumberOfAllocations; ++i)
      Allocate1();
  }
};

class MyThread2 : public SimpleThread {
 public:
  MyThread2() : SimpleThread("MyThread2") {}
  void Run() override {
    for (int i = 0; i < kNumberOfAllocations; ++i)
      Allocate2();
  }
};

void CheckAllocationPattern(void (*allocate_callback)()) {
  auto* profiler = SamplingHeapProfiler::Get();
  PoissonAllocationSampler::Get()->SuppressRandomnessForTest(false);
  profiler->SetSamplingInterval(10240);
  base::TimeTicks t0 = base::TimeTicks::Now();
  std::map<size_t, size_t> sums;
  const int iterations = 40;
  for (int i = 0; i < iterations; ++i) {
    uint32_t id = profiler->Start();
    allocate_callback();
    std::vector<SamplingHeapProfiler::Sample> samples =
        profiler->GetSamples(id);
    profiler->Stop();
    std::map<size_t, size_t> buckets;
    for (auto& sample : samples) {
      buckets[sample.size] += sample.total;
    }
    for (auto& it : buckets) {
      if (it.first != 400 && it.first != 700 && it.first != 20480)
        continue;
      sums[it.first] += it.second;
      printf("%zu,", it.second);
    }
    printf("\n");
  }

  printf("Time taken %" PRIu64 "ms\n",
         (base::TimeTicks::Now() - t0).InMilliseconds());

  for (auto sum : sums) {
    intptr_t expected = sum.first * kNumberOfAllocations;
    intptr_t actual = sum.second / iterations;
    printf("%zu:\tmean: %zu\trelative error: %.2f%%\n", sum.first, actual,
           100. * (actual - expected) / expected);
  }
}

// Manual tests to check precision of the sampling profiler.
// Yes, they do leak lots of memory.

TEST_F(SamplingHeapProfilerTest, DISABLED_ParallelLargeSmallStats) {
  CheckAllocationPattern([]() {
    SimpleThread* t1 = new MyThread1();
    SimpleThread* t2 = new MyThread2();
    t1->Start();
    t2->Start();
    for (int i = 0; i < kNumberOfAllocations; ++i)
      Allocate3();
    t1->Join();
    t2->Join();
  });
}

TEST_F(SamplingHeapProfilerTest, DISABLED_SequentialLargeSmallStats) {
  CheckAllocationPattern([]() {
    for (int i = 0; i < kNumberOfAllocations; ++i) {
      Allocate1();
      Allocate2();
      Allocate3();
    }
  });
}

}  // namespace base
