/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/traced/probes/common/android_cpu_per_uid_poller.h"

#include <cctype>
#include <optional>
#include <vector>

#include "perfetto/base/logging.h"
#include "src/android_internal/cpu_time_in_state.h"
#include "src/android_internal/lazy_library_loader.h"

namespace perfetto {

namespace {
constexpr uint32_t kInvalidUid = 0xffffffff;

bool ExistsNonZero(const std::vector<uint64_t>& cluster_deltas_ms) {
  for (uint64_t value : cluster_deltas_ms) {
    if (value != 0) {
      return true;
    }
  }
  return false;
}

constexpr size_t kMaxNumResults = 4096;
}  // namespace

// Dynamically loads the libperfetto_android_internal.so library which
// allows to proxy calls to android hwbinder in in-tree builds.
struct AndroidCpuPerUidPoller::DynamicLibLoader {
  PERFETTO_LAZY_LOAD(android_internal::GetCpuTimes, get_cpu_times_);

  std::vector<android_internal::CpuTime> GetCpuTimes(uint64_t* last_update_ns) {
    if (!get_cpu_times_) {
      return std::vector<android_internal::CpuTime>();
    }

    std::vector<android_internal::CpuTime> cpu_time(kMaxNumResults);
    size_t num_results = cpu_time.size();
    if (!get_cpu_times_(&cpu_time[0], &num_results, last_update_ns)) {
      num_results = 0;
    }
    cpu_time.resize(num_results);
    return cpu_time;
  }
};

AndroidCpuPerUidPoller::AndroidCpuPerUidPoller() {}

AndroidCpuPerUidPoller::~AndroidCpuPerUidPoller() = default;

void AndroidCpuPerUidPoller::Start() {
  lib_.reset(new DynamicLibLoader());
}

std::vector<CpuPerUidTime> AndroidCpuPerUidPoller::Poll() {
  std::vector<android_internal::CpuTime> cpu_times =
      lib_->GetCpuTimes(&last_update_ns_);

  std::vector<CpuPerUidTime> result;
  std::vector<uint64_t> cluster_deltas_ms;
  uint32_t first_uid = kInvalidUid;
  uint32_t current_uid = kInvalidUid;

  // GetCpuTimes() returns values grouped by UID.
  for (auto& time : cpu_times) {
    if (first_uid == kInvalidUid) {
      first_uid = time.uid;
    }

    // Determine the number of clusters from the first UID. They should all be
    // the same.
    if (time.uid == first_uid) {
      cluster_deltas_ms.push_back(0L);
    }

    if (time.uid != current_uid) {
      if (current_uid != kInvalidUid) {
        if (ExistsNonZero(cluster_deltas_ms)) {
          result.emplace_back(current_uid, cluster_deltas_ms);
        }
      }
      current_uid = time.uid;
      for (uint64_t& val : cluster_deltas_ms) {
        val = 0;
      }
    }

    if (time.cluster >= cluster_deltas_ms.size()) {
      // Data is corrupted
      continue;
    }

    uint64_t key = ((uint64_t(current_uid)) << 32) | time.cluster;
    uint64_t* previous = previous_times_.Find(key);
    if (previous) {
      cluster_deltas_ms[time.cluster] = time.total_time_ms - *previous;
      *previous = time.total_time_ms;
    } else {
      cluster_deltas_ms[time.cluster] = time.total_time_ms;
      previous_times_.Insert(key, time.total_time_ms);
    }
  }
  if (ExistsNonZero(cluster_deltas_ms)) {
    result.emplace_back(current_uid, cluster_deltas_ms);
  }
  return result;
}

void AndroidCpuPerUidPoller::Clear() {
  previous_times_.Clear();
}

}  // namespace perfetto
