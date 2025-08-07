/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/profiling/memory/java_hprof_producer.h"

#include <signal.h>
#include <limits>
#include <optional>

#include "perfetto/ext/tracing/core/trace_writer.h"
#include "src/profiling/common/proc_cmdline.h"
#include "src/profiling/common/proc_utils.h"
#include "src/profiling/common/producer_support.h"

namespace perfetto {
namespace profiling {
namespace {

constexpr int kJavaHeapprofdSignal = __SIGRTMIN + 6;
constexpr uint32_t kInitialConnectionBackoffMs = 100;
constexpr uint32_t kMaxConnectionBackoffMs = 30 * 1000;
constexpr const char* kJavaHprofDataSource = "android.java_hprof";

}  // namespace

void JavaHprofProducer::DoContinuousDump(DataSourceInstanceID id,
                                         uint32_t dump_interval) {
  auto it = data_sources_.find(id);
  if (it == data_sources_.end())
    return;
  DataSource& ds = it->second;
  if (!ds.config().continuous_dump_config().scan_pids_only_on_start()) {
    ds.CollectPids();
  }
  ds.SendSignal();
  auto weak_producer = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_producer, id, dump_interval] {
        if (!weak_producer)
          return;
        weak_producer->DoContinuousDump(id, dump_interval);
      },
      dump_interval);
}

JavaHprofProducer::DataSource::DataSource(
    DataSourceConfig ds_config,
    JavaHprofConfig config,
    std::vector<std::string> target_cmdlines)
    : ds_config_(std::move(ds_config)),
      config_(std::move(config)),
      target_cmdlines_(std::move(target_cmdlines)) {}

void JavaHprofProducer::DataSource::SendSignal() const {
  for (pid_t pid : pids_) {
    auto opt_status = ReadStatus(pid);
    if (!opt_status) {
      PERFETTO_PLOG("Failed to read /proc/%d/status. Not signalling.", pid);
      continue;
    }
    auto uids = GetUids(*opt_status);
    if (!uids) {
      PERFETTO_ELOG(
          "Failed to read Uid from /proc/%d/status. "
          "Not signalling.",
          pid);
      continue;
    }
    if (!CanProfile(ds_config_, uids->effective,
                    config_.target_installed_by())) {
      PERFETTO_ELOG("%d (UID %" PRIu64 ") not profileable.", pid,
                    uids->effective);
      continue;
    }
    PERFETTO_DLOG("Sending %d to %d", kJavaHeapprofdSignal, pid);
    union sigval signal_value;
    signal_value.sival_int = static_cast<int32_t>(
        ds_config_.tracing_session_id() % std::numeric_limits<int32_t>::max());
    if (sigqueue(pid, kJavaHeapprofdSignal, signal_value) != 0) {
      PERFETTO_DPLOG("sigqueue");
    }
  }
}

void JavaHprofProducer::DataSource::CollectPids() {
  pids_.clear();
  for (uint64_t pid : config_.pid()) {
    pids_.insert(static_cast<pid_t>(pid));
  }
  glob_aware::FindPidsForCmdlinePatterns(target_cmdlines_, &pids_);
  if (config_.min_anonymous_memory_kb() > 0)
    RemoveUnderAnonThreshold(config_.min_anonymous_memory_kb(), &pids_);
}

void JavaHprofProducer::IncreaseConnectionBackoff() {
  connection_backoff_ms_ *= 2;
  if (connection_backoff_ms_ > kMaxConnectionBackoffMs)
    connection_backoff_ms_ = kMaxConnectionBackoffMs;
}

void JavaHprofProducer::ResetConnectionBackoff() {
  connection_backoff_ms_ = kInitialConnectionBackoffMs;
}

void JavaHprofProducer::SetupDataSource(DataSourceInstanceID id,
                                        const DataSourceConfig& ds_config) {
  if (data_sources_.find(id) != data_sources_.end()) {
    PERFETTO_DFATAL_OR_ELOG("Duplicate data source: %" PRIu64, id);
    return;
  }
  JavaHprofConfig config;
  config.ParseFromString(ds_config.java_hprof_config_raw());
  std::vector<std::string> cmdline_patterns = config.process_cmdline();
  DataSource ds(ds_config, std::move(config), std::move(cmdline_patterns));
  ds.CollectPids();
  data_sources_.emplace(id, ds);
}

void JavaHprofProducer::StartDataSource(DataSourceInstanceID id,
                                        const DataSourceConfig&) {
  auto it = data_sources_.find(id);
  if (it == data_sources_.end()) {
    PERFETTO_DFATAL_OR_ELOG("Starting invalid data source: %" PRIu64, id);
    return;
  }
  const DataSource& ds = it->second;
  const auto& continuous_dump_config = ds.config().continuous_dump_config();
  uint32_t dump_interval = continuous_dump_config.dump_interval_ms();
  if (dump_interval) {
    auto weak_producer = weak_factory_.GetWeakPtr();
    task_runner_->PostDelayedTask(
        [weak_producer, id, dump_interval] {
          if (!weak_producer)
            return;
          weak_producer->DoContinuousDump(id, dump_interval);
        },
        continuous_dump_config.dump_phase_ms());
  }
  ds.SendSignal();
}

void JavaHprofProducer::StopDataSource(DataSourceInstanceID id) {
  auto it = data_sources_.find(id);
  if (it == data_sources_.end()) {
    PERFETTO_DFATAL_OR_ELOG("Stopping invalid data source: %" PRIu64, id);
    return;
  }
  data_sources_.erase(it);
}

void JavaHprofProducer::Flush(FlushRequestID flush_id,
                              const DataSourceInstanceID*,
                              size_t,
                              FlushFlags) {
  endpoint_->NotifyFlushComplete(flush_id);
}

void JavaHprofProducer::OnConnect() {
  PERFETTO_DCHECK(state_ == kConnecting);
  state_ = kConnected;
  ResetConnectionBackoff();
  PERFETTO_LOG("Connected to the service.");

  DataSourceDescriptor desc;
  desc.set_name(kJavaHprofDataSource);
  endpoint_->RegisterDataSource(desc);
}

void JavaHprofProducer::Restart() {
  // We lost the connection with the tracing service. At this point we need
  // to reset all the data sources. Trying to handle that manually is going to
  // be error prone. What we do here is simply destroy the instance and
  // recreate it again.
  base::TaskRunner* task_runner = task_runner_;
  const char* socket_name = producer_sock_name_;

  // Invoke destructor and then the constructor again.
  this->~JavaHprofProducer();
  new (this) JavaHprofProducer(task_runner);

  ConnectWithRetries(socket_name);
}

void JavaHprofProducer::ConnectWithRetries(const char* socket_name) {
  PERFETTO_DCHECK(state_ == kNotStarted);
  state_ = kNotConnected;

  ResetConnectionBackoff();
  producer_sock_name_ = socket_name;
  ConnectService();
}

void JavaHprofProducer::SetProducerEndpoint(
    std::unique_ptr<TracingService::ProducerEndpoint> endpoint) {
  PERFETTO_DCHECK(state_ == kNotConnected || state_ == kNotStarted);
  state_ = kConnecting;
  endpoint_ = std::move(endpoint);
}

void JavaHprofProducer::ConnectService() {
  SetProducerEndpoint(ProducerIPCClient::Connect(
      producer_sock_name_, this, "android.java_hprof", task_runner_));
}

void JavaHprofProducer::OnDisconnect() {
  PERFETTO_DCHECK(state_ == kConnected || state_ == kConnecting);
  PERFETTO_LOG("Disconnected from tracing service");

  auto weak_producer = weak_factory_.GetWeakPtr();
  if (state_ == kConnected)
    return task_runner_->PostTask([weak_producer] {
      if (!weak_producer)
        return;
      weak_producer->Restart();
    });

  state_ = kNotConnected;
  IncreaseConnectionBackoff();
  task_runner_->PostDelayedTask(
      [weak_producer] {
        if (!weak_producer)
          return;
        weak_producer->ConnectService();
      },
      connection_backoff_ms_);
}

}  // namespace profiling
}  // namespace perfetto
