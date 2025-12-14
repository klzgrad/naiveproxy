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
#include <memory>
#include <optional>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "src/traced/probes/common/android_cpu_per_uid_poller.h"

#include "protos/perfetto/config/android/cpu_per_uid_config.pbzero.h"
#include "protos/perfetto/trace/android/cpu_per_uid_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace {
constexpr uint32_t kMinPollIntervalMs = 10;
constexpr uint32_t kDefaultPollIntervalMs = 1000;
}  // namespace

// static
const ProbesDataSource::Descriptor AndroidCpuPerUidDataSource::descriptor = {
    /*name*/ "android.cpu_per_uid",
    /*flags*/ Descriptor::kHandlesIncrementalState,
    /*fill_descriptor_func*/ nullptr,
};

AndroidCpuPerUidDataSource::AndroidCpuPerUidDataSource(
    const DataSourceConfig& cfg,
    base::TaskRunner* task_runner,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      poller_(std::make_unique<AndroidCpuPerUidPoller>()),
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
  poller_->Start();
  Tick();
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
  std::vector<CpuPerUidTime> cpu_times = poller_->Poll();

  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));

  if (first_time_) {
    packet->set_sequence_flags(
        protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);
  } else {
    packet->set_sequence_flags(
        protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
  }

  auto* proto = packet->set_cpu_per_uid_data();

  protozero::PackedVarInt uid_list;
  protozero::PackedVarInt total_time_ms_list;

  for (auto& time : cpu_times) {
    uid_list.Append(time.uid);
    for (uint64_t value : time.time_delta_ms) {
      total_time_ms_list.Append(value);
    }
  }

  if (first_time_ && cpu_times.size() > 0) {
    proto->set_cluster_count(uint32_t(cpu_times[0].time_delta_ms.size()));
    first_time_ = false;
  }
  proto->set_uid(uid_list);
  proto->set_total_time_ms(total_time_ms_list);
}

void AndroidCpuPerUidDataSource::Flush(FlushRequestID,
                                       std::function<void()> callback) {
  writer_->Flush(callback);
}

void AndroidCpuPerUidDataSource::ClearIncrementalState() {
  poller_->Clear();
  first_time_ = true;
}

}  // namespace perfetto
