/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/traced/probes/common/cpu_freq_info_for_testing.h"

#include <memory>

namespace perfetto {

namespace {

const char kCpuFrequenciesAndroidLittleCore[] =
    "300000 576000 748800 998400 1209600 1324800 1516800 1612800 1708800 \n";

const char kCpuBoostFrequenciesAndroidLittleCore[] = "\n";

const char kCpuFrequenciesAndroidBigCore[] =
    "300000 652800 825600 979200 1132800 1363200 1536000 1747200 1843200 "
    "1996800 \n";

const char kCpuBoostFrequenciesAndroidBigCore[] = "2803200 \n";

}  // namespace

CpuFreqInfoForTesting::CpuFreqInfoForTesting() {
  // Create a subset of /sys/devices/system/cpu.
  tmpdir_.AddDir("cpuidle");
  tmpdir_.AddDir("cpu0");
  tmpdir_.AddDir("cpu0/cpufreq");
  tmpdir_.AddFile("cpu0/cpufreq/scaling_available_frequencies",
                  kCpuFrequenciesAndroidLittleCore);
  tmpdir_.AddFile("cpu0/cpufreq/scaling_boost_frequencies",
                  kCpuBoostFrequenciesAndroidLittleCore);
  tmpdir_.AddFile("cpu0/cpufreq/scaling_cur_freq", "2650000");
  tmpdir_.AddDir("cpufreq");
  tmpdir_.AddDir("cpu1");
  tmpdir_.AddDir("cpu1/cpufreq");
  tmpdir_.AddFile("cpu1/cpufreq/scaling_available_frequencies",
                  kCpuFrequenciesAndroidBigCore);
  tmpdir_.AddFile("cpu1/cpufreq/scaling_boost_frequencies",
                  kCpuBoostFrequenciesAndroidBigCore);
  tmpdir_.AddFile("cpu1/cpufreq/scaling_cur_freq", "3698200");
  tmpdir_.AddDir("power");
}

std::unique_ptr<CpuFreqInfo> CpuFreqInfoForTesting::GetInstance() {
  return std::unique_ptr<CpuFreqInfo>(new CpuFreqInfo(tmpdir_.path()));
}

}  // namespace perfetto
