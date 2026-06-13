// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <benchmark/benchmark.h>

#include <atomic>
#include <random>
#include <string>
#include <thread>

#include "perfetto/ext/base/rt_mutex.h"

namespace {

bool IsBenchmarkFunctionalOnly() {
  return getenv("BENCHMARK_FUNCTIONAL_TEST_ONLY") != nullptr;
}

void BenchmarkArgs(benchmark::internal::Benchmark* b) {
  b->UseRealTime();
}

}  // namespace

template <typename MutexType>
static void BM_RtMutex_NoContention(benchmark::State& state) {
  MutexType mutex;

  // Prepare a vector of pointers to simulate pointer chasing.
  constexpr size_t kPointerChaseSize = 64;
  std::vector<std::string> data(kPointerChaseSize, "someSampleText123");
  std::vector<std::string*> ptrs;
  for (auto& s : data)
    ptrs.push_back(&s);

  // Shuffle to simulate random memory access
  std::mt19937 rng(42);
  std::shuffle(ptrs.begin(), ptrs.end(), rng);
  std::hash<std::string> hasher;
  size_t dummy = 0;

  for (auto _ : state) {
    mutex.lock();

    // Simulate pointer chasing + workload
    for (size_t i = 0; i < 8; ++i) {
      std::string& str = *ptrs[i % kPointerChaseSize];
      for (char& c : str)
        c = static_cast<char>(std::toupper(c));
      dummy += hasher(str);
    }
    benchmark::DoNotOptimize(dummy);
    mutex.unlock();
    benchmark::ClobberMemory();
  }
}

template <typename MutexType>
static void BM_RtMutex_Contention(benchmark::State& state) {
  constexpr int kNumThreads = 4;
  MutexType mutex;
  int counter = 0;
  std::atomic<bool> stop{false};

  auto worker = [&]() {
    while (!stop.load(std::memory_order_relaxed)) {
      mutex.lock();
      counter++;
      benchmark::DoNotOptimize(counter);
      mutex.unlock();
    }
  };

  for (auto _ : state) {
    stop.store(false);
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; ++i)
      threads.emplace_back(worker);

    std::this_thread::sleep_for(IsBenchmarkFunctionalOnly()
                                    ? std::chrono::milliseconds(1)
                                    : std::chrono::seconds(1));
    stop.store(true);
    for (auto& t : threads)
      t.join();
  }

  state.counters["Iterations"] = static_cast<double>(counter);
}

BENCHMARK_TEMPLATE(BM_RtMutex_NoContention, std::mutex)->Apply(BenchmarkArgs);
BENCHMARK_TEMPLATE(BM_RtMutex_Contention, std::mutex)->Apply(BenchmarkArgs);

#if PERFETTO_HAS_RT_FUTEX()
using perfetto::base::internal::RtFutex;
BENCHMARK_TEMPLATE(BM_RtMutex_NoContention, RtFutex)->Apply(BenchmarkArgs);
BENCHMARK_TEMPLATE(BM_RtMutex_Contention, RtFutex)->Apply(BenchmarkArgs);
#endif

#if PERFETTO_HAS_POSIX_RT_MUTEX()
using perfetto::base::internal::RtPosixMutex;
BENCHMARK_TEMPLATE(BM_RtMutex_NoContention, RtPosixMutex)->Apply(BenchmarkArgs);
BENCHMARK_TEMPLATE(BM_RtMutex_Contention, RtPosixMutex)->Apply(BenchmarkArgs);
#endif
