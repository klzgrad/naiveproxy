/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an "AS
 * IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language
 * governing permissions and limitations under the License.
 */
#include "src/traced/probes/probes_producer.h"

#include <stdio.h>
#include <sys/stat.h>

#include <memory>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/crash_keys.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/unix_socket.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/watchdog.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/priority_boost_config.h"
#include "perfetto/ext/tracing/ipc/producer_ipc_client.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "src/traced/probes/android_cpu_per_uid/android_cpu_per_uid_data_source.h"
#include "src/traced/probes/android_game_intervention_list/android_game_intervention_list_data_source.h"
#include "src/traced/probes/android_kernel_wakelocks/android_kernel_wakelocks_data_source.h"
#include "src/traced/probes/android_log/android_log_data_source.h"
#include "src/traced/probes/android_system_property/android_system_property_data_source.h"
#include "src/traced/probes/filesystem/inode_file_data_source.h"
#include "src/traced/probes/ftrace/frozen_ftrace_data_source.h"
#include "src/traced/probes/ftrace/ftrace_data_source.h"
#include "src/traced/probes/initial_display_state/initial_display_state_data_source.h"
#include "src/traced/probes/metatrace/metatrace_data_source.h"
#include "src/traced/probes/packages_list/packages_list_data_source.h"
#include "src/traced/probes/power/android_power_data_source.h"
#include "src/traced/probes/power/linux_power_sysfs_data_source.h"
#include "src/traced/probes/probes_data_source.h"
#include "src/traced/probes/ps/process_stats_data_source.h"
#include "src/traced/probes/statsd_client/statsd_binder_data_source.h"
#include "src/traced/probes/sys_stats/sys_stats_data_source.h"
#include "src/traced/probes/system_info/system_info_data_source.h"
#include "src/traced/probes/user_list/user_list_data_source.h"

namespace perfetto {
namespace {

constexpr uint32_t kInitialConnectionBackoffMs = 100;
constexpr uint32_t kMaxConnectionBackoffMs = 30 * 1000;

// Should be larger than FtraceController::kControllerFlushTimeoutMs.
constexpr uint32_t kFlushTimeoutMs = 1000;

constexpr size_t kTracingSharedMemSizeHintBytes = 2 * 1024 * 1024;
constexpr size_t kTracingSharedMemPageSizeHintBytes = 32 * 1024;

base::CrashKey g_crash_key_ds_count("ds_instance_count");
base::CrashKey g_crash_key_session_count("tracing_session_count");

}  // namespace

// State transition diagram:
//                    +----------------------------+
//                    v                            +
// NotStarted -> NotConnected -> Connecting -> Connected
//                    ^              +
//                    +--------------+
//

ProbesProducer* ProbesProducer::instance_ = nullptr;

ProbesProducer* ProbesProducer::GetInstance() {
  return instance_;
}

ProbesProducer::ProbesProducer() : weak_factory_(this) {
  PERFETTO_CHECK(instance_ == nullptr);
  instance_ = this;
}

ProbesProducer::~ProbesProducer() {
  instance_ = nullptr;
  // The ftrace data sources must be deleted before the ftrace controller.
  data_sources_.clear();
  ftrace_controller_.reset();
}

void ProbesProducer::Restart() {
  // We lost the connection with the tracing service. At this point we need
  // to reset all the data sources. Trying to handle that manually is going to
  // be error prone. What we do here is simply destroying the instance and
  // recreating it again.

  base::TaskRunner* task_runner = task_runner_;
  const char* socket_name = socket_name_;

  // Invoke destructor and then the constructor again.
  this->~ProbesProducer();
  new (this) ProbesProducer();

  ConnectWithRetries(socket_name, task_runner);
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<FtraceDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  // Don't retry if FtraceController::Create() failed once.
  // This can legitimately happen on user builds where we cannot access the
  // debug paths, e.g., because of SELinux rules.
  if (ftrace_creation_failed_)
    return nullptr;

  FtraceConfig ftrace_config;
  ftrace_config.ParseFromString(config.ftrace_config_raw());
  // Lazily create on the first instance.
  if (!ftrace_controller_) {
    ftrace_controller_ = FtraceController::Create(task_runner_, this);

    if (!ftrace_controller_) {
      PERFETTO_ELOG("Failed to create FtraceController");
      ftrace_creation_failed_ = true;
      return nullptr;
    }
  }

  PERFETTO_LOG("Ftrace setup (target_buf=%" PRIu32 ")", config.target_buffer());
  const BufferID buffer_id = static_cast<BufferID>(config.target_buffer());
  std::unique_ptr<FtraceDataSource> data_source(new FtraceDataSource(
      ftrace_controller_->GetWeakPtr(), session_id, std::move(ftrace_config),
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall)));
  if (!ftrace_controller_->AddDataSource(data_source.get())) {
    PERFETTO_ELOG("Failed to setup ftrace");
    return nullptr;
  }
  return std::unique_ptr<ProbesDataSource>(std::move(data_source));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<InodeFileDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& source_config) {
  PERFETTO_LOG("Inode file map setup (target_buf=%" PRIu32 ")",
               source_config.target_buffer());
  auto buffer_id = static_cast<BufferID>(source_config.target_buffer());
  if (system_inodes_.empty())
    CreateStaticDeviceToInodeMap("/system", &system_inodes_);
  return std::make_unique<InodeFileDataSource>(
      source_config, task_runner_, session_id, &system_inodes_, &cache_,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<ProcessStatsDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<ProcessStatsDataSource>(
      task_runner_, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall),
      config);
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<StatsdBinderDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<StatsdBinderDataSource>(
      task_runner_, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall),
      config);
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<AndroidPowerDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<AndroidPowerDataSource>(
      config, task_runner_, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<LinuxPowerSysfsDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<LinuxPowerSysfsDataSource>(
      config, task_runner_, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<AndroidCpuPerUidDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<AndroidCpuPerUidDataSource>(
      config, task_runner_, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<AndroidKernelWakelocksDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<AndroidKernelWakelocksDataSource>(
      config, task_runner_, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<AndroidLogDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<AndroidLogDataSource>(
      config, task_runner_, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<PackagesListDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<PackagesListDataSource>(
      config, task_runner_, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<AndroidGameInterventionListDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<AndroidGameInterventionListDataSource>(
      config, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<SysStatsDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<SysStatsDataSource>(
      task_runner_, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall),
      config, std::make_unique<CpuFreqInfo>());
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<MetatraceDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<MetatraceDataSource>(
      task_runner_, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<SystemInfoDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<SystemInfoDataSource>(
      session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall),
      std::make_unique<CpuFreqInfo>());
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<UserListDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::unique_ptr<ProbesDataSource>(new UserListDataSource(
      config, session_id,
      endpoint_->CreateTraceWriter(buffer_id,
                                   perfetto::BufferExhaustedPolicy::kStall)));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<InitialDisplayStateDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<InitialDisplayStateDataSource>(
      task_runner_, config, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<AndroidSystemPropertyDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<AndroidSystemPropertyDataSource>(
      task_runner_, config, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

template <>
std::unique_ptr<ProbesDataSource>
ProbesProducer::CreateDSInstance<FrozenFtraceDataSource>(
    TracingSessionID session_id,
    const DataSourceConfig& config) {
  auto buffer_id = static_cast<BufferID>(config.target_buffer());
  return std::make_unique<FrozenFtraceDataSource>(
      task_runner_, config, session_id,
      endpoint_->CreateTraceWriter(buffer_id, BufferExhaustedPolicy::kStall));
}

// Another anonymous namespace. This cannot be moved into the anonymous
// namespace on top (it would fail to compile), because the CreateDSInstance
// methods need to be fully declared before.
namespace {

using ProbesDataSourceFactoryFunc = std::unique_ptr<ProbesDataSource> (
    ProbesProducer::*)(TracingSessionID, const DataSourceConfig&);

struct DataSourceTraits {
  const ProbesDataSource::Descriptor* descriptor;
  ProbesDataSourceFactoryFunc factory_func;
};

template <typename T>
constexpr DataSourceTraits Ds() {
  return DataSourceTraits{&T::descriptor, &ProbesProducer::CreateDSInstance<T>};
}

constexpr const DataSourceTraits kAllDataSources[] = {
    Ds<AndroidGameInterventionListDataSource>(),
    Ds<AndroidCpuPerUidDataSource>(),
    Ds<AndroidKernelWakelocksDataSource>(),
    Ds<AndroidLogDataSource>(),
    Ds<AndroidPowerDataSource>(),
    Ds<AndroidSystemPropertyDataSource>(),
    Ds<FrozenFtraceDataSource>(),
    Ds<FtraceDataSource>(),
    Ds<InitialDisplayStateDataSource>(),
    Ds<InodeFileDataSource>(),
    Ds<LinuxPowerSysfsDataSource>(),
    Ds<MetatraceDataSource>(),
    Ds<PackagesListDataSource>(),
    Ds<ProcessStatsDataSource>(),
#if PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)
    Ds<StatsdBinderDataSource>(),
#endif
    Ds<SysStatsDataSource>(),
    Ds<SystemInfoDataSource>(),
    Ds<UserListDataSource>(),
};

}  // namespace

void ProbesProducer::OnConnect() {
  PERFETTO_DCHECK(state_ == kConnecting);
  state_ = kConnected;
  sock_inotify_.reset();
  ResetConnectionBackoff();
  PERFETTO_LOG("Connected to the service");

  std::array<DataSourceDescriptor, base::ArraySize(kAllDataSources)>
      proto_descs;
  // Generate all data source descriptors.
  for (size_t i = 0; i < proto_descs.size(); i++) {
    DataSourceDescriptor& proto_desc = proto_descs[i];
    const ProbesDataSource::Descriptor* desc = kAllDataSources[i].descriptor;
    for (size_t j = i + 1; j < proto_descs.size(); j++) {
      if (kAllDataSources[i].descriptor == kAllDataSources[j].descriptor) {
        PERFETTO_FATAL("Duplicate descriptor name %s",
                       kAllDataSources[i].descriptor->name);
      }
    }

    proto_desc.set_name(desc->name);
    proto_desc.set_will_notify_on_start(true);
    proto_desc.set_will_notify_on_stop(true);
    using Flags = ProbesDataSource::Descriptor::Flags;
    if (desc->flags & Flags::kHandlesIncrementalState)
      proto_desc.set_handles_incremental_state_clear(true);
    if (desc->fill_descriptor_func) {
      desc->fill_descriptor_func(&proto_desc);
    }
  }

  // Register all the data sources. Separate from the above loop because, if
  // generating a data source descriptor takes too long, we don't want to be in
  // a state where only some data sources are registered.
  for (const DataSourceDescriptor& proto_desc : proto_descs) {
    endpoint_->RegisterDataSource(proto_desc);
  }

  // Used by tracebox to synchronize with traced_probes being registered.
  if (all_data_sources_registered_cb_) {
    endpoint_->Sync(all_data_sources_registered_cb_);
  }
}

void ProbesProducer::OnDisconnect() {
  PERFETTO_DCHECK(state_ == kConnected || state_ == kConnecting);
  PERFETTO_LOG("Disconnected from tracing service");

  if (state_ == kConnected)
    return task_runner_->PostTask([this] { this->Restart(); });

  state_ = kNotConnected;
  IncreaseConnectionBackoff();

  auto reconnect_task = [weak_this = weak_factory_.GetWeakPtr()] {
    if (weak_this && weak_this->state_ == kNotConnected)
      weak_this->Connect();
  };

  task_runner_->PostDelayedTask(reconnect_task, connection_backoff_ms_);
  if (!sock_inotify_) {
    sock_inotify_ = base::WatchUnixSocketCreation(task_runner_, socket_name_,
                                                  reconnect_task);
  }
}

void ProbesProducer::SetupDataSource(DataSourceInstanceID instance_id,
                                     const DataSourceConfig& config) {
  PERFETTO_DLOG("SetupDataSource(id=%" PRIu64 ", name=%s)", instance_id,
                config.name().c_str());
  PERFETTO_DCHECK(data_sources_.count(instance_id) == 0);
  TracingSessionID session_id = config.tracing_session_id();
  PERFETTO_CHECK(session_id > 0);

  std::unique_ptr<ProbesDataSource> data_source;

  for (const DataSourceTraits& rds : kAllDataSources) {
    if (rds.descriptor->name != config.name()) {
      continue;
    }
    data_source = (this->*(rds.factory_func))(session_id, config);
    break;
  }

  if (!data_source) {
    PERFETTO_ELOG("Failed to create data source '%s'", config.name().c_str());
    return;
  }

  if (config.has_priority_boost()) {
    auto sched_policy = CreateSchedPolicyFromConfig(config.priority_boost());
    if (!sched_policy.ok()) {
      PERFETTO_ELOG("Invalid priority boost config for data source '%s': %s",
                    config.name().c_str(), sched_policy.status().c_message());
    } else {
      auto boost = base::ScopedSchedBoost::Boost(sched_policy.value());
      if (!boost.ok()) {
        PERFETTO_ELOG("Failed to boost priority for data source '%s': %s",
                      config.name().c_str(), boost.status().c_message());
      } else {
        data_source->priority_boost = std::move(*boost);
      }
    }
  }

  session_data_sources_[session_id].emplace(data_source->descriptor,
                                            data_source.get());
  data_sources_[instance_id] = std::move(data_source);

  // Set crash keys for debugging overload crashes.
  g_crash_key_ds_count.Set(static_cast<int64_t>(data_sources_.size()));
  g_crash_key_session_count.Set(
      static_cast<int64_t>(session_data_sources_.size()));
}

void ProbesProducer::StartDataSource(DataSourceInstanceID instance_id,
                                     const DataSourceConfig& config) {
  PERFETTO_DLOG("StartDataSource(id=%" PRIu64 ", name=%s)", instance_id,
                config.name().c_str());
  auto it = data_sources_.find(instance_id);
  if (it == data_sources_.end()) {
    // Can happen if SetupDataSource() failed (e.g. ftrace was busy).
    PERFETTO_ELOG("Data source id=%" PRIu64 " not found", instance_id);
    return;
  }
  ProbesDataSource* data_source = it->second.get();
  if (data_source->started)
    return;
  if (config.trace_duration_ms() != 0) {
    // We need to ensure this timeout is worse than the worst case
    // time from us starting to traced managing to disable us.
    // See b/236814186#comment8 for context
    // Note: when using prefer_suspend_clock_for_duration the actual duration
    // might be < timeout measured in in wall time. But this is fine
    // because the resulting timeout will be conservative (it will be accurate
    // if the device never suspends, and will be more lax if it does).
    uint32_t timeout =
        2 * (kDefaultFlushTimeoutMs + config.trace_duration_ms() +
             config.stop_timeout_ms());
    watchdogs_.emplace(
        instance_id, base::Watchdog::GetInstance()->CreateFatalTimer(
                         timeout, base::WatchdogCrashReason::kTraceDidntStop));
  }
  data_source->started = true;
  data_source->Start();
  endpoint_->NotifyDataSourceStarted(instance_id);
}

void ProbesProducer::StopDataSource(DataSourceInstanceID id) {
  PERFETTO_LOG("Producer stop (id=%" PRIu64 ")", id);
  auto it = data_sources_.find(id);
  if (it == data_sources_.end()) {
    // Can happen if SetupDataSource() failed (e.g. ftrace was busy).
    PERFETTO_ELOG("Cannot stop data source id=%" PRIu64 ", not found", id);
    return;
  }
  ProbesDataSource* data_source = it->second.get();

  // MetatraceDataSource special case: re-flush to record the final flushes of
  // other data sources.
  if (data_source->descriptor == &MetatraceDataSource::descriptor)
    data_source->Flush(FlushRequestID{0}, [] {});

  TracingSessionID session_id = data_source->tracing_session_id;

  auto session_it = session_data_sources_.find(session_id);
  if (session_it != session_data_sources_.end()) {
    auto desc_range = session_it->second.equal_range(data_source->descriptor);
    for (auto ds_it = desc_range.first; ds_it != desc_range.second; ds_it++) {
      if (ds_it->second == data_source) {
        session_it->second.erase(ds_it);
        if (session_it->second.empty()) {
          session_data_sources_.erase(session_it);
        }
        break;
      }
    }
  }
  data_sources_.erase(it);
  watchdogs_.erase(id);

  // We could (and used to) acknowledge the stop before tearing the local state
  // down, allowing the tracing service and the consumer to carry on quicker.
  // However in the case of tracebox, the traced_probes subprocess gets killed
  // as soon as the trace is considered finished (i.e. all data source stops
  // were acked), and therefore the kill would race against the tracefs
  // cleanup.
  endpoint_->NotifyDataSourceStopped(id);

  // This is to reduce the noise in Android performance benchmarks that measure
  // the memory of perfetto processes.
  base::MaybeReleaseAllocatorMemToOS();

  // Set crash keys for debugging overload crashes.
  g_crash_key_ds_count.Set(static_cast<int64_t>(data_sources_.size()));
  g_crash_key_session_count.Set(
      static_cast<int64_t>(session_data_sources_.size()));
}

void ProbesProducer::OnTracingSetup() {
  // shared_memory() can be null in test environments when running in-process.
  if (endpoint_->shared_memory()) {
    base::Watchdog::GetInstance()->SetMemoryLimit(
        endpoint_->shared_memory()->size() + base::kWatchdogDefaultMemorySlack,
        base::kWatchdogDefaultMemoryWindow);
  }
}

void ProbesProducer::Flush(FlushRequestID flush_request_id,
                           const DataSourceInstanceID* data_source_ids,
                           size_t num_data_sources,
                           FlushFlags) {
  PERFETTO_DLOG("ProbesProducer::Flush(%" PRIu64 ") begin", flush_request_id);
  PERFETTO_DCHECK(flush_request_id);
  auto log_on_exit = base::OnScopeExit([&] {
    PERFETTO_DLOG("ProbesProducer::Flush(%" PRIu64 ") end", flush_request_id);
  });

  // Issue a Flush() to all started data sources.
  std::vector<std::pair<DataSourceInstanceID, ProbesDataSource*>> ds_to_flush;
  for (size_t i = 0; i < num_data_sources; i++) {
    DataSourceInstanceID ds_id = data_source_ids[i];
    auto it = data_sources_.find(ds_id);
    if (it == data_sources_.end() || !it->second->started)
      continue;
    pending_flushes_.emplace(flush_request_id, ds_id);
    ds_to_flush.emplace_back(ds_id, it->second.get());
  }

  // If there is nothing to flush, ack immediately.
  if (ds_to_flush.empty()) {
    endpoint_->NotifyFlushComplete(flush_request_id);
    return;
  }

  // Otherwise post the timeout task and issue all flushes in order.
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this, flush_request_id] {
        if (weak_this)
          weak_this->OnFlushTimeout(flush_request_id);
      },
      kFlushTimeoutMs);

  // Issue all the flushes in order. We do this in a separate loop to deal with
  // the case of data sources invoking the callback synchronously (b/295189870).
  for (const auto& kv : ds_to_flush) {
    const DataSourceInstanceID ds_id = kv.first;
    ProbesDataSource* const data_source = kv.second;
    auto flush_callback = [weak_this, flush_request_id, ds_id] {
      if (weak_this)
        weak_this->OnDataSourceFlushComplete(flush_request_id, ds_id);
    };
    PERFETTO_DLOG("Flushing data source %" PRIu64 " %s", ds_id,
                  data_source->descriptor->name);
    data_source->Flush(flush_request_id, flush_callback);
  }
}

void ProbesProducer::OnDataSourceFlushComplete(FlushRequestID flush_request_id,
                                               DataSourceInstanceID ds_id) {
  PERFETTO_DLOG("Flush %" PRIu64 " acked by data source %" PRIu64,
                flush_request_id, ds_id);
  auto range = pending_flushes_.equal_range(flush_request_id);
  for (auto it = range.first; it != range.second; it++) {
    if (it->second == ds_id) {
      pending_flushes_.erase(it);
      break;
    }
  }

  if (pending_flushes_.count(flush_request_id))
    return;  // Still waiting for other data sources to ack.

  PERFETTO_DLOG("All data sources acked to flush %" PRIu64, flush_request_id);
  endpoint_->NotifyFlushComplete(flush_request_id);
}

void ProbesProducer::OnFlushTimeout(FlushRequestID flush_request_id) {
  if (pending_flushes_.count(flush_request_id) == 0)
    return;  // All acked.
  PERFETTO_ELOG("Flush(%" PRIu64 ") timed out", flush_request_id);
  pending_flushes_.erase(flush_request_id);
  endpoint_->NotifyFlushComplete(flush_request_id);
}

void ProbesProducer::ClearIncrementalState(
    const DataSourceInstanceID* data_source_ids,
    size_t num_data_sources) {
  for (size_t i = 0; i < num_data_sources; i++) {
    DataSourceInstanceID ds_id = data_source_ids[i];
    auto it = data_sources_.find(ds_id);
    if (it == data_sources_.end() || !it->second->started)
      continue;

    it->second->ClearIncrementalState();
  }
}

// This function is called by the FtraceController in batches, whenever it has
// read one or more pages from one or more cpus and written that into the
// userspace tracing buffer. If more than one ftrace data sources are active,
// this call typically happens after writing for all session has been handled.
void ProbesProducer::OnFtraceDataWrittenIntoDataSourceBuffers() {
  for (const auto& tracing_session : session_data_sources_) {
    // Take the metadata (e.g. new pids) collected from ftrace and pass it to
    // other interested data sources (e.g. the process scraper to get command
    // lines on new pids and tgid<>tid mappings). Note: there can be more than
    // one ftrace data source per session. All of them should be considered
    // (b/169226092).
    const std::unordered_multimap<const ProbesDataSource::Descriptor*,
                                  ProbesDataSource*>& ds_by_type =
        tracing_session.second;
    auto ft_range = ds_by_type.equal_range(&FtraceDataSource::descriptor);

    auto ino_range = ds_by_type.equal_range(&InodeFileDataSource::descriptor);
    auto ps_range = ds_by_type.equal_range(&ProcessStatsDataSource::descriptor);
    for (auto ft_it = ft_range.first; ft_it != ft_range.second; ft_it++) {
      auto* ftrace_ds = static_cast<FtraceDataSource*>(ft_it->second);
      if (!ftrace_ds->started)
        continue;
      auto* metadata = ftrace_ds->mutable_metadata();
      for (auto ps_it = ps_range.first; ps_it != ps_range.second; ps_it++) {
        auto* ps_ds = static_cast<ProcessStatsDataSource*>(ps_it->second);
        if (!ps_ds->started || !ps_ds->on_demand_dumps_enabled())
          continue;
        // Ordering the rename pids before the seen pids is important so that
        // any renamed processes get scraped in the OnPids call.
        if (!metadata->rename_pids.empty())
          ps_ds->OnRenamePids(metadata->rename_pids);
        if (!metadata->pids.empty())
          ps_ds->OnPids(metadata->pids);
        if (!metadata->fds.empty())
          ps_ds->OnFds(metadata->fds);
      }
      for (auto in_it = ino_range.first; in_it != ino_range.second; in_it++) {
        auto* inode_ds = static_cast<InodeFileDataSource*>(in_it->second);
        if (!inode_ds->started)
          continue;
        inode_ds->OnInodes(metadata->inode_and_device);
      }
      metadata->Clear();
    }  // for (FtraceDataSource)
  }  // for (tracing_session)
}

void ProbesProducer::ConnectWithRetries(const char* socket_name,
                                        base::TaskRunner* task_runner) {
  PERFETTO_DCHECK(state_ == kNotStarted);
  state_ = kNotConnected;

  ResetConnectionBackoff();
  socket_name_ = socket_name;
  task_runner_ = task_runner;
  Connect();
}

void ProbesProducer::Connect() {
  PERFETTO_DCHECK(state_ == kNotConnected);
  state_ = kConnecting;
  endpoint_ = ProducerIPCClient::Connect(
      socket_name_, this, "perfetto.traced_probes", task_runner_,
      TracingService::ProducerSMBScrapingMode::kDisabled,
      kTracingSharedMemSizeHintBytes, kTracingSharedMemPageSizeHintBytes);
}

void ProbesProducer::IncreaseConnectionBackoff() {
  connection_backoff_ms_ *= 2;
  if (connection_backoff_ms_ > kMaxConnectionBackoffMs)
    connection_backoff_ms_ = kMaxConnectionBackoffMs;
}

void ProbesProducer::ResetConnectionBackoff() {
  connection_backoff_ms_ = kInitialConnectionBackoffMs;
}

void ProbesProducer::ActivateTrigger(std::string trigger) {
  task_runner_->PostTask(
      [this, trigger]() { endpoint_->ActivateTriggers({trigger}); });
}

}  // namespace perfetto
