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

#include "src/traced/probes/common/cpu_freq_info.h"

#include <unistd.h>

#include <set>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto {

namespace {

using CpuAndFreq = std::pair</* cpu */ uint32_t, /* freq */ uint32_t>;

// Reads space-separated CPU frequencies from a sysfs file.
void ReadAndAppendFreqs(std::set<CpuAndFreq>* freqs,
                        uint32_t cpu_index,
                        const std::string& sys_cpu_freqs) {
  base::StringSplitter entries(sys_cpu_freqs, ' ');
  while (entries.Next()) {
    auto freq = base::StringToUInt32(entries.cur_token());
    if (freq.has_value())
      freqs->insert({cpu_index, freq.value()});
  }
}

}  // namespace

CpuFreqInfo::CpuFreqInfo(std::string sysfs_cpu_path)
    : sysfs_cpu_path_{sysfs_cpu_path} {
  base::ScopedDir cpu_dir(opendir(sysfs_cpu_path_.c_str()));
  if (!cpu_dir) {
    PERFETTO_PLOG("Failed to opendir(%s)", sysfs_cpu_path_.c_str());
    return;
  }
  // Accumulate cpu and freqs into a set to ensure stable order.
  std::set<CpuAndFreq> freqs;
  while (struct dirent* dir_ent = readdir(*cpu_dir)) {
    if (dir_ent->d_type != DT_DIR)
      continue;
    std::string dir_name(dir_ent->d_name);
    if (!base::StartsWith(dir_name, "cpu"))
      continue;
    auto maybe_cpu_index =
        base::StringToUInt32(base::StripPrefix(dir_name, "cpu"));
    // There are some directories (cpufreq, cpuidle) which should be skipped.
    if (!maybe_cpu_index.has_value())
      continue;
    uint32_t cpu_index = maybe_cpu_index.value();
    ReadAndAppendFreqs(
        &freqs, cpu_index,
        ReadFile(sysfs_cpu_path_ + "/cpu" + std::to_string(cpu_index) +
                 "/cpufreq/scaling_available_frequencies"));
    ReadAndAppendFreqs(
        &freqs, cpu_index,
        ReadFile(sysfs_cpu_path_ + "/cpu" + std::to_string(cpu_index) +
                 "/cpufreq/scaling_boost_frequencies"));
  }

  // Build index with guards.
  uint32_t last_cpu = 0;
  uint32_t index = 0;
  frequencies_index_.push_back(0);
  for (const auto& cpu_freq : freqs) {
    frequencies_.push_back(cpu_freq.second);
    if (cpu_freq.first != last_cpu)
      frequencies_index_.push_back(index);
    last_cpu = cpu_freq.first;
    index++;
  }
  frequencies_.push_back(0);
  frequencies_index_.push_back(index);
}

CpuFreqInfo::~CpuFreqInfo() = default;

CpuFreqInfo::Range CpuFreqInfo::GetFreqs(uint32_t cpu) {
  if (cpu >= frequencies_index_.size() - 1) {
    PERFETTO_DLOG("No frequencies for cpu%" PRIu32, cpu);
    const uint32_t* end = frequencies_.data() + frequencies_.size();
    return {end, end};
  }
  auto* start = &frequencies_[frequencies_index_[cpu]];
  auto* end = &frequencies_[frequencies_index_[cpu + 1]];
  return {start, end};
}

uint32_t CpuFreqInfo::GetCpuFreqIndex(uint32_t cpu, uint32_t freq) {
  auto range = GetFreqs(cpu);
  uint32_t index = 0;
  for (const uint32_t* it = range.first; it != range.second; it++, index++) {
    if (*it == freq) {
      return static_cast<uint32_t>(frequencies_index_[cpu]) + index + 1;
    }
  }
  return 0;
}

std::string CpuFreqInfo::ReadFile(std::string path) {
  std::string contents;
  if (!base::ReadFile(path, &contents))
    return "";
  return contents;
}

const std::vector<uint32_t>& CpuFreqInfo::ReadCpuCurrFreq() {
  // Check if capacity of cpu_curr_freq_ is enough for all CPUs
  auto num_cpus = static_cast<size_t>(sysconf(_SC_NPROCESSORS_CONF));
  if (cpu_curr_freq_.size() < num_cpus)
    cpu_curr_freq_.resize(num_cpus);

  for (uint32_t i = 0; i < cpu_curr_freq_.size(); i++) {
    // Read CPU current frequency. Set 0 for offline/disabled cpus.
    std::string buf(ReadFile(sysfs_cpu_path_ + "/cpu" + std::to_string(i) +
                             "/cpufreq/scaling_cur_freq"));
    if (buf.empty()) {
      cpu_curr_freq_[i] = 0;
      continue;
    }
    auto freq = base::StringToUInt32(base::StripSuffix(buf, "\n"));
    if (!freq.has_value()) {
      cpu_curr_freq_[i] = 0;
      continue;
    }
    cpu_curr_freq_[i] = freq.value();
  }
  return cpu_curr_freq_;
}

}  // namespace perfetto
