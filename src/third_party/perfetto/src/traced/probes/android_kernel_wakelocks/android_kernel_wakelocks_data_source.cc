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

#include "src/traced/probes/android_kernel_wakelocks/android_kernel_wakelocks_data_source.h"

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
#include "src/android_internal/lazy_library_loader.h"
#include "src/android_internal/suspend_control_service.h"
#include "src/kernel_utils/kernel_wakelock_errors.h"

#include "protos/perfetto/common/android_energy_consumer_descriptor.pbzero.h"
#include "protos/perfetto/config/android/kernel_wakelocks_config.pbzero.h"
#include "protos/perfetto/trace/android/kernel_wakelock_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

struct KernelWakelockInfo {
  uint32_t id;
  uint64_t last_value;
};

namespace {
constexpr uint32_t kMinPollIntervalMs = 100;
constexpr uint32_t kDefaultPollIntervalMs = 1000;
constexpr size_t kMaxNumWakelocks = 1024;
}  // namespace

// static
const ProbesDataSource::Descriptor
    AndroidKernelWakelocksDataSource::descriptor = {
        /*name*/ "android.kernel_wakelocks",
        /*flags*/ Descriptor::kHandlesIncrementalState,
        /*fill_descriptor_func*/ nullptr,
};

// Dynamically loads the libperfetto_android_internal.so library which
// allows to proxy calls to android hwbinder in in-tree builds.
struct AndroidKernelWakelocksDataSource::DynamicLibLoader {
  PERFETTO_LAZY_LOAD(android_internal::GetKernelWakelocks,
                     get_kernel_wakelocks_);

  std::vector<android_internal::KernelWakelock> GetKernelWakelocks() {
    if (!get_kernel_wakelocks_)
      return std::vector<android_internal::KernelWakelock>();

    std::vector<android_internal::KernelWakelock> wakelock(kMaxNumWakelocks);
    size_t num_wakelocks = wakelock.size();
    if (!get_kernel_wakelocks_(&wakelock[0], &num_wakelocks)) {
      PERFETTO_ELOG("Failed to retrieve kernel wakelocks.");
      num_wakelocks = 0;
    }
    wakelock.resize(num_wakelocks);
    return wakelock;
  }
};

AndroidKernelWakelocksDataSource::AndroidKernelWakelocksDataSource(
    const DataSourceConfig& cfg,
    base::TaskRunner* task_runner,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {
  using protos::pbzero::KernelWakelocksConfig;
  KernelWakelocksConfig::Decoder kcfg(cfg.kernel_wakelocks_config_raw());
  poll_interval_ms_ = kcfg.poll_ms();

  if (poll_interval_ms_ == 0)
    poll_interval_ms_ = kDefaultPollIntervalMs;

  if (poll_interval_ms_ < kMinPollIntervalMs) {
    PERFETTO_ELOG("Kernel wakelock poll interval of %" PRIu32
                  " ms is too low. Capping to %" PRIu32 " ms",
                  poll_interval_ms_, kMinPollIntervalMs);
    poll_interval_ms_ = kMinPollIntervalMs;
  }

  // Really it shouldn't be more than poll_interval_ms_ but allow for
  // some clock skew; the implausible values we receive seem to be very large
  // in practice.
  max_plausible_diff_ms_ = 10 * poll_interval_ms_;
}

AndroidKernelWakelocksDataSource::~AndroidKernelWakelocksDataSource() = default;

void AndroidKernelWakelocksDataSource::Start() {
  lib_.reset(new DynamicLibLoader());
  Tick();
}

void AndroidKernelWakelocksDataSource::Tick() {
  // Post next task.
  auto now_ms = base::GetWallTimeMs().count();
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this] {
        if (weak_this)
          weak_this->Tick();
      },
      poll_interval_ms_ - static_cast<uint32_t>(now_ms % poll_interval_ms_));

  WriteKernelWakelocks();
}

void AndroidKernelWakelocksDataSource::WriteKernelWakelocks() {
  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));

  if (wakelocks_.size() == 0) {
    packet->set_sequence_flags(
        protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED |
        protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
  } else {
    packet->set_sequence_flags(
        protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);
  }

  // Some native wakelocks can have duplicated names; merge them before
  // calculating deltas.
  base::FlatHashMap<std::string, uint64_t> totals;

  auto* proto = packet->set_kernel_wakelock_data();
  uint64_t error_flags = 0;

  std::vector<android_internal::KernelWakelock> wakelocks =
      lib_->GetKernelWakelocks();
  for (const auto& wakelock : wakelocks) {
    std::string name = std::string(wakelock.wakelock_name);
    totals[name] += wakelock.total_time_ms;

    auto [info, inserted] = wakelocks_.Insert(name, KernelWakelockInfo{});
    if (inserted) {
      info->id = ++next_id_;
      info->last_value = 0;
      auto* wakelock_descriptor = proto->add_wakelock();
      wakelock_descriptor->set_wakelock_id(info->id);
      wakelock_descriptor->set_wakelock_name(name);
      wakelock_descriptor->set_wakelock_type(
          wakelock.is_kernel ? protos::pbzero::KernelWakelockData::Wakelock::
                                   Type::WAKELOCK_TYPE_KERNEL
                             : protos::pbzero::KernelWakelockData::Wakelock::
                                   Type::WAKELOCK_TYPE_NATIVE);
    }
  }

  protozero::PackedVarInt wakelock_id;
  protozero::PackedVarInt time_held_millis;

  for (auto it = totals.GetIterator(); it; ++it) {
    KernelWakelockInfo* info = wakelocks_.Find(it.key());
    uint64_t total = it.value();
    uint64_t last_value = info->last_value;

    if (total == 0) {
      error_flags |= kKernelWakelockErrorZeroValue;
      continue;
    } else if (total < last_value) {
      error_flags |= kKernelWakelockErrorNonMonotonicValue;
      continue;
    }
    if (total != last_value) {
      uint64_t diff = total - last_value;
      // From observation, if SuspendControlService gives us a very large
      // value it's a one-off, so don't let it define the new normal.
      if (last_value > 0 && diff > max_plausible_diff_ms_) {
        error_flags |= kKernelWakelockErrorImplausiblyLargeValue;
        continue;
      }
      info->last_value = total;
      wakelock_id.Append(info->id);
      time_held_millis.Append(diff);
    }
  }

  proto->set_wakelock_id(wakelock_id);
  proto->set_time_held_millis(time_held_millis);

  if (error_flags != 0) {
    proto->set_error_flags(error_flags);
  }
}

void AndroidKernelWakelocksDataSource::Flush(FlushRequestID,
                                             std::function<void()> callback) {
  writer_->Flush(callback);
}

void AndroidKernelWakelocksDataSource::ClearIncrementalState() {
  wakelocks_.Clear();
  next_id_ = 0;
}

}  // namespace perfetto
