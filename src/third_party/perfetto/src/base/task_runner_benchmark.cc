// Copyright (C) 2021 The Android Open Source Project
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

#include <array>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "perfetto/ext/base/lock_free_task_runner.h"
#include "perfetto/ext/base/unix_task_runner.h"
#include "perfetto/ext/base/waitable_event.h"

namespace perfetto {
namespace base {
namespace {

// This matrix size has been tuned looking at a perf profiling of this benchmark
// and making sure that the Hash and Rotate costs is roughly ~60% of the whole
// cpu time (to simulate some realistic workload).
constexpr int kMatrixSize = 16;
using Matrix = std::array<std::array<int, kMatrixSize>, kMatrixSize>;

Matrix Rotate(const Matrix& m) {
  Matrix res;
  for (size_t r = 0; r < kMatrixSize; ++r) {
    for (size_t c = 0; c < kMatrixSize; ++c) {
      res[r][c] = m[kMatrixSize - c - 1][r];
    }
  }
  return res;
}

uint64_t Hash(const Matrix& m) {
  uint64_t hash = 0;
  for (size_t r = 0; r < kMatrixSize; ++r) {
    for (size_t c = 0; c < kMatrixSize; ++c) {
      hash = (hash << 5) + hash + static_cast<uint64_t>(m[r][c]);
    }
  }
  return hash;
}

// Single-threaded benchmark
template <typename TaskRunnerType>
static void BM_TaskRunner_SingleThreaded(benchmark::State& state) {
  const uint32_t kNumTasks = 10000;
  TaskRunnerType task_runner;
  std::minstd_rand0 rng(0);
  Matrix matrix{};
  uint64_t hash_val = 0;
  std::function<void()> task_fn;
  uint32_t num_tasks = 0;
  task_fn = [&] {
    matrix = Rotate(matrix);
    hash_val = Hash(matrix);
    uint32_t task_id = ++num_tasks;
    if (task_id < kNumTasks) {
      task_runner.PostTask(task_fn);
      if (task_id % 128 == 0) {
        // Emulate a burst of 100 extra tasks ocne every 128 tasks.
        for (int j = 0; j < 100; ++j) {
          task_runner.PostTask([&] { Rotate(matrix); });
        }
      }
    } else {
      task_runner.Quit();
    }
  };

  for (auto _ : state) {
    num_tasks = 0;
    task_runner.PostTask(task_fn);
    task_runner.Run();
    benchmark::DoNotOptimize(hash_val);
  }
}

BENCHMARK_TEMPLATE(BM_TaskRunner_SingleThreaded, UnixTaskRunner);
BENCHMARK_TEMPLATE(BM_TaskRunner_SingleThreaded, LockFreeTaskRunner);

// In this benchmark there is one task runner, and 8 threads that post tasks
// on it. The post happens in bursts: all the thread post one task each, then
// they wait that the 8th task has completed, then they post again. In this way
// they PostTask at the same time causing contention but are also sensitive to
// PostTask latency.
template <typename TaskRunnerType>
static void BM_TaskRunner_MultiThreaded(benchmark::State& state) {
  constexpr uint32_t kNumThreads = 8;
  constexpr uint32_t kNumRounds = 10;
  constexpr uint32_t kNumTasks = kNumThreads * kNumRounds;
  std::vector<std::thread> threads;
  Matrix matrix{};
  uint32_t num_tasks = 0;
  WaitableEvent burst_done;
  TaskRunnerType task_runner;
  std::atomic<bool> quit_threads{};

  std::function<void()> task;
  task = [&] {
    matrix = Rotate(matrix);
    benchmark::DoNotOptimize(Hash(matrix));
    uint32_t task_id = num_tasks++;
    if (task_id >= kNumTasks) {
      task_runner.Quit();
      return;
    }
    if (task_id % kNumThreads == kNumThreads - 1) {
      burst_done.Notify();
    }
  };

  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&] {
      for (uint32_t n = 0; !quit_threads; ++n) {
        burst_done.Wait(n);
        task_runner.PostTask(task);
      }
    });
  }

  for (auto _ : state) {
    num_tasks = 0;
    burst_done.Notify();
    task_runner.Run();
  }
  quit_threads = true;
  burst_done.Notify();

  for (auto& thread : threads) {
    thread.join();
  }
}

BENCHMARK_TEMPLATE(BM_TaskRunner_MultiThreaded, UnixTaskRunner);
BENCHMARK_TEMPLATE(BM_TaskRunner_MultiThreaded, LockFreeTaskRunner);

}  // namespace
}  // namespace base
}  // namespace perfetto
