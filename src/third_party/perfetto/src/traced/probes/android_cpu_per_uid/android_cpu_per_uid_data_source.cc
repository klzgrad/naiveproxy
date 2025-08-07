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

#include "src/traced/probes/android_cpu_per_uid/android_cpu_per_uid_data_source.h"

#include <cctype>
#include <optional>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "src/android_internal/cpu_time_in_state.h"
#include "src/android_internal/lazy_library_loader.h"

#include "protos/perfetto/config/android/cpu_per_uid_config.pbzero.h"
#include "protos/perfetto/trace/android/cpu_per_uid_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace {
constexpr uint32_t kMinPollIntervalMs = 10;
constexpr uint32_t kDefaultPollIntervalMs = 1000;
constexpr uint32_t kInvalidUid = 0xffffffff;
constexpr size_t kMaxNumResults = 4096;

void MaybeAppendUid(uint32_t uid,
                    std::vector<uint64_t>& cluster_deltas_ms,
                    protozero::PackedVarInt& uid_list,
                    protozero::PackedVarInt& total_time_ms_list) {
  bool write = false;
  for (uint64_t value : cluster_deltas_ms) {
    if (value != 0) {
      write = true;
    }
  }

  if (write) {
    uid_list.Append(uid);
    for (uint64_t value : cluster_deltas_ms) {
      total_time_ms_list.Append(value);
    }
  }
}

}  // namespace

// static
const ProbesDataSource::Descriptor AndroidCpuPerUidDataSource::descriptor = {
    /*name*/ "android.cpu_per_uid",
    /*flags*/ Descriptor::kHandlesIncrementalState,
    /*fill_descriptor_func*/ nullptr,
};

// Dynamically loads the libperfetto_android_internal.so library which
// allows to proxy calls to android hwbinder in in-tree builds.
struct AndroidCpuPerUidDataSource::DynamicLibLoader {
  PERFETTO_LAZY_LOAD(android_internal::EnsureCpuTimesAvailable,
                     ensure_cpu_times_available_);
  PERFETTO_LAZY_LOAD(android_internal::GetCpuTimes, get_cpu_times_);

  bool EnsureCpuTimesAvailable() {
    if (!ensure_cpu_times_available_) {
      return false;
    }

    return ensure_cpu_times_available_();
  }

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

AndroidCpuPerUidDataSource::AndroidCpuPerUidDataSource(
    const DataSourceConfig& cfg,
    base::TaskRunner* task_runner,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {
  using protos::pbzero::CpuPerUidConfig;
  CpuPerUidConfig::Decoder cpu_cfg(cfg.cpu_per_uid_config_raw());
  poll_interval_ms_ = cpu_cfg.poll_ms();

  if (poll_interval_ms_ == 0)
    poll_interval_ms_ = kDefaultPollIntervalMs;

  if (poll_interval_ms_ < kMinPollIntervalMs) {
    PERFETTO_ELOG("CPU per UID poll interval of %" PRIu32
                  " ms is too low. Capping to %" PRIu32 " ms",
                  poll_interval_ms_, kMinPollIntervalMs);
    poll_interval_ms_ = kMinPollIntervalMs;
  }
}

AndroidCpuPerUidDataSource::~AndroidCpuPerUidDataSource() = default;

void AndroidCpuPerUidDataSource::Start() {
  lib_.reset(new DynamicLibLoader());
  if (lib_->EnsureCpuTimesAvailable()) {
    Tick();
  } else {
    PERFETTO_ELOG("Could not enable CPU per UID data source");
  }
}

void AndroidCpuPerUidDataSource::Tick() {
  // Post next task.
  auto now_ms = base::GetWallTimeMs().count();
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this] {
        if (weak_this)
          weak_this->Tick();
      },
      poll_interval_ms_ - static_cast<uint32_t>(now_ms % poll_interval_ms_));

  WriteCpuPerUid();
}

void AndroidCpuPerUidDataSource::WriteCpuPerUid() {
  std::vector<android_internal::CpuTime> cpu_times =
      lib_->GetCpuTimes(&last_update_ns_);

  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));

  bool first_time;
  if (previous_times_.size() == 0) {
    first_time = true;
    packet->set_sequence_flags(
        protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);
  } else {
    first_time = false;
    packet->set_sequence_flags(
        protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
  }

  auto* proto = packet->set_cpu_per_uid_data();

  protozero::PackedVarInt uid_list;
  protozero::PackedVarInt total_time_ms_list;
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
        MaybeAppendUid(current_uid, cluster_deltas_ms, uid_list,
                       total_time_ms_list);
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
  MaybeAppendUid(current_uid, cluster_deltas_ms, uid_list, total_time_ms_list);

  if (first_time) {
    proto->set_cluster_count(uint32_t(cluster_deltas_ms.size()));
  }
  proto->set_uid(uid_list);
  proto->set_total_time_ms(total_time_ms_list);
}

void AndroidCpuPerUidDataSource::Flush(FlushRequestID,
                                       std::function<void()> callback) {
  writer_->Flush(callback);
}

void AndroidCpuPerUidDataSource::ClearIncrementalState() {
  previous_times_.Clear();
}

}  // namespace perfetto
