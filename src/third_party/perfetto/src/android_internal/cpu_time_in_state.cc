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
#include "src/android_internal/cpu_time_in_state.h"

#include <memory>
#include <unordered_map>
#include <vector>

#define LOG_TAG "perfetto"

#include <log/log.h>

#include <cputimeinstate.h>

namespace perfetto {
namespace android_internal {

bool GetCpuTimes(CpuTime* cpu_times,
                 size_t* size_of_arr,
                 uint64_t* last_update_ns) {
  std::optional<
      std::unordered_map<uint32_t, std::vector<std::vector<uint64_t>>>>
      data = android::bpf::getUidsUpdatedCpuFreqTimes(last_update_ns);
  if (data) {
    const size_t in_array_size = *size_of_arr;
    *size_of_arr = 0;

    bool full = false;
    for (auto& [uid, times] : *data) {
      // outer vector is cluster, inner is frequency
      if (times.empty())
        continue;
      for (uint32_t cluster = 0; cluster < times.size(); cluster++) {
        if (*size_of_arr >= in_array_size) {
          full = true;
          break;
        }
        uint64_t total_time_ns = 0;
        auto& cur = cpu_times[(*size_of_arr)++];
        for (auto& cluster_freq_time_ns : times[cluster]) {
          total_time_ns += cluster_freq_time_ns;
        }
        cur.uid = uid;
        cur.cluster = cluster;
        cur.total_time_ms = total_time_ns / 1000000;
      }
      if (full) {
        break;
      }
    }

    return true;
  }

  return false;
}

}  // namespace android_internal
}  // namespace perfetto
