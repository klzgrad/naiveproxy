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

#ifndef SRC_TRACED_PROBES_COMMON_ANDROID_CPU_PER_UID_POLLER_H_
#define SRC_TRACED_PROBES_COMMON_ANDROID_CPU_PER_UID_POLLER_H_

#include <memory>
#include <vector>
#include "perfetto/ext/base/flat_hash_map.h"

namespace perfetto {

struct CpuPerUidTime {
  CpuPerUidTime(uint32_t uid_in, std::vector<uint64_t> time_delta_ms_in)
      : uid(uid_in), time_delta_ms(time_delta_ms_in) {}

  uint32_t uid;
  std::vector<uint64_t> time_delta_ms;
};

class AndroidCpuPerUidPoller {
 public:
  AndroidCpuPerUidPoller();

  ~AndroidCpuPerUidPoller();

  void Start();

  std::vector<CpuPerUidTime> Poll();

  void Clear();

 private:
  struct DynamicLibLoader;
  std::unique_ptr<DynamicLibLoader> lib_;
  uint64_t last_update_ns_ = 0;
  base::FlatHashMap<uint64_t, uint64_t> previous_times_;
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_COMMON_ANDROID_CPU_PER_UID_POLLER_H_
