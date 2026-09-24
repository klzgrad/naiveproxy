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

#include "src/traced/service/builtin_producer.h"

#include <sys/types.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/proc_utils.h"
#include "perfetto/ext/base/metatrace.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/client_identity.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/ext/tracing/core/tracing_service.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "src/tracing/service/metatrace_writer.h"

#include "protos/perfetto/config/android/android_sdk_sysprop_guard_config.pbzero.h"

// This translation unit is only ever used in Android in-tree builds.
// These producers are here  to dynamically start heapprofd and other services
// via sysprops when a trace that requests them is active. That can only happen
// in in-tree builds of Android.

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <sys/system_properties.h>
#endif

namespace perfetto {

namespace {

constexpr char kHeapprofdDataSourceName[] = "android.heapprofd";
constexpr char kJavaHprofDataSourceName[] = "android.java_hprof";
constexpr char kJavaHprofOomDataSourceName[] = "android.java_hprof.oom";
constexpr char kTracedPerfDataSourceName[] = "linux.perf";
constexpr char kLazyHeapprofdPropertyName[] = "traced.lazy.heapprofd";
constexpr char kLazyTracedPerfPropertyName[] = "traced.lazy.traced_perf";
constexpr char kJavaHprofOomActivePropertyName[] =
    "traced.oome_heap_session.count";

constexpr char kAndroidSdkSyspropGuardDataSourceName[] =
    "android.sdk_sysprop_guard";
constexpr char kPerfettoSdkSyspropGuardGenerationPropertyName[] =
    "debug.tracing.ctl.perfetto.sdk_sysprop_guard_generation";
constexpr char kHwuiSkiaBroadTracingPropertyName[] =
    "debug.tracing.ctl.hwui.skia_tracing_enabled";
constexpr char kHwuiSkiaUsePerfettoPropertyName[] =
    "debug.tracing.ctl.hwui.skia_use_perfetto_track_events";
constexpr char kHwuiSkiaPropertyPackageSeparator[] = ".";
constexpr char kSurfaceFlingerSkiaBroadTracingPropertyName[] =
    "debug.tracing.ctl.renderengine.skia_tracing_enabled";
constexpr char kSurfaceFlingerSkiaUsePerfettoPropertyName[] =
    "debug.tracing.ctl.renderengine.skia_use_perfetto_track_events";

}  // namespace

BuiltinProducer::BuiltinProducer(base::TaskRunner* task_runner,
                                 uint32_t lazy_stop_delay_ms)
    : task_runner_(task_runner), weak_factory_(this) {
  lazy_heapprofd_.stop_delay_ms = lazy_stop_delay_ms;
  lazy_traced_perf_.stop_delay_ms = lazy_stop_delay_ms;
}

BuiltinProducer::~BuiltinProducer() {
  if (!lazy_heapprofd_.instance_ids.empty())
    SetAndroidProperty(kLazyHeapprofdPropertyName, "");
  if (!lazy_traced_perf_.instance_ids.empty())
    SetAndroidProperty(kLazyTracedPerfPropertyName, "");
  if (!java_hprof_oome_instances_.empty())
    SetAndroidProperty(kJavaHprofOomActivePropertyName, "");
}

void BuiltinProducer::ConnectInProcess(TracingService* svc) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  // TODO(primiano): ConnectProducer should take a base::PlatformProcessId not
  // pid_t, as they are different on Windows. But that is a larger refactoring
  // and not worth given this is the only use case where it clashes.
  const pid_t cur_proc_id = 0;
#else
  const pid_t cur_proc_id = base::GetProcessId();
#endif
  endpoint_ = svc->ConnectProducer(
      this, ClientIdentity(base::GetCurrentUserId(), cur_proc_id), "traced",
      /*shared_memory_size_hint_bytes=*/16 * 1024, /*in_process=*/true,
      TracingService::ProducerSMBScrapingMode::kDisabled,
      /*shared_memory_page_size_hint_bytes=*/4096);
}

void BuiltinProducer::OnConnect() {
  DataSourceDescriptor metatrace_dsd;
  metatrace_dsd.set_name(MetatraceWriter::kDataSourceName);
  metatrace_dsd.set_will_notify_on_stop(true);
  endpoint_->RegisterDataSource(metatrace_dsd);
  {
    DataSourceDescriptor lazy_heapprofd_dsd;
    lazy_heapprofd_dsd.set_name(kHeapprofdDataSourceName);
    endpoint_->RegisterDataSource(lazy_heapprofd_dsd);
  }
  {
    DataSourceDescriptor lazy_java_hprof_dsd;
    lazy_java_hprof_dsd.set_name(kJavaHprofDataSourceName);
    endpoint_->RegisterDataSource(lazy_java_hprof_dsd);
  }
  {
    DataSourceDescriptor lazy_traced_perf_dsd;
    lazy_traced_perf_dsd.set_name(kTracedPerfDataSourceName);
    endpoint_->RegisterDataSource(lazy_traced_perf_dsd);
  }
  {
    DataSourceDescriptor java_hprof_oome_dsd;
    java_hprof_oome_dsd.set_name(kJavaHprofOomDataSourceName);
    endpoint_->RegisterDataSource(java_hprof_oome_dsd);
  }
  {
    DataSourceDescriptor track_event_dsd;
    track_event_dsd.set_name(kAndroidSdkSyspropGuardDataSourceName);
    endpoint_->RegisterDataSource(track_event_dsd);
  }
}

void BuiltinProducer::SetupDataSource(DataSourceInstanceID ds_id,
                                      const DataSourceConfig& ds_config) {
  if (ds_config.name() == kHeapprofdDataSourceName ||
      ds_config.name() == kJavaHprofDataSourceName) {
    SetAndroidProperty(kLazyHeapprofdPropertyName, "1");
    lazy_heapprofd_.generation++;
    lazy_heapprofd_.instance_ids.emplace(ds_id);
    return;
  }

  if (ds_config.name() == kTracedPerfDataSourceName) {
    SetAndroidProperty(kLazyTracedPerfPropertyName, "1");
    lazy_traced_perf_.generation++;
    lazy_traced_perf_.instance_ids.emplace(ds_id);
    return;
  }

  if (ds_config.name() == kJavaHprofOomDataSourceName) {
    java_hprof_oome_instances_.emplace(ds_id);
    SetAndroidProperty(kJavaHprofOomActivePropertyName,
                       std::to_string(java_hprof_oome_instances_.size()));
    return;
  }

  // TODO(b/281329340): delete this when no longer needed.
  if (ds_config.name() == kAndroidSdkSyspropGuardDataSourceName) {
    protos::pbzero::AndroidSdkSyspropGuardConfig::Decoder sysprop_guard_config(
        ds_config.android_sdk_sysprop_guard_config_raw());
    std::vector<std::string> hwui_package_name_filter;
    for (auto package = sysprop_guard_config.hwui_package_name_filter();
         package; ++package) {
      hwui_package_name_filter.emplace_back((*package).ToStdString());
    }

    bool increase_generation = false;

    // SurfaceFlinger / RenderEngine
    if (sysprop_guard_config.surfaceflinger_skia_track_events() &&
        !android_sdk_sysprop_guard_state_.surfaceflinger_initialized) {
      SetAndroidProperty(kSurfaceFlingerSkiaBroadTracingPropertyName, "true");
      SetAndroidProperty(kSurfaceFlingerSkiaUsePerfettoPropertyName, "true");
      android_sdk_sysprop_guard_state_.surfaceflinger_initialized = true;
      increase_generation = true;
    }

    // HWUI apps
    if (sysprop_guard_config.hwui_skia_track_events()) {
      if (hwui_package_name_filter.size() > 0) {
        // Set per-app flags
        for (std::string package : hwui_package_name_filter) {
          if (android_sdk_sysprop_guard_state_.hwui_packages_initialized.count(
                  package) == 0) {
            SetAndroidProperty(
                kHwuiSkiaBroadTracingPropertyName +
                    (kHwuiSkiaPropertyPackageSeparator + package),
                "true");
            SetAndroidProperty(
                kHwuiSkiaUsePerfettoPropertyName +
                    (kHwuiSkiaPropertyPackageSeparator + package),
                "true");
            android_sdk_sysprop_guard_state_.hwui_packages_initialized.insert(
                package);
            increase_generation = true;
          }
        }
      } else if (!android_sdk_sysprop_guard_state_.hwui_globally_initialized) {
        // Set global flag
        SetAndroidProperty(kHwuiSkiaBroadTracingPropertyName, "true");
        SetAndroidProperty(kHwuiSkiaUsePerfettoPropertyName, "true");
        android_sdk_sysprop_guard_state_.hwui_globally_initialized = true;
        increase_generation = true;
      }
    }

    if (increase_generation) {
      android_sdk_sysprop_guard_state_.generation++;
      SetAndroidProperty(
          kPerfettoSdkSyspropGuardGenerationPropertyName,
          std::to_string(android_sdk_sysprop_guard_state_.generation));
    }

    return;
  }
}

void BuiltinProducer::StartDataSource(DataSourceInstanceID ds_id,
                                      const DataSourceConfig& ds_config) {
  // We slightly rely on the fact that since this producer is in-process for
  // enabling metatrace early (relative to producers that are notified via IPC).
  if (ds_config.name() == MetatraceWriter::kDataSourceName) {
    auto writer = endpoint_->CreateTraceWriter(
        static_cast<BufferID>(ds_config.target_buffer()),
        BufferExhaustedPolicy::kStall);

    auto it_and_inserted = metatrace_.writers.emplace(
        std::piecewise_construct, std::make_tuple(ds_id), std::make_tuple());
    PERFETTO_DCHECK(it_and_inserted.second);
    // Note: only the first concurrent writer will actually be active.
    metatrace_.writers[ds_id].Enable(task_runner_, std::move(writer),
                                     metatrace::TAG_ANY);
  }
}

void BuiltinProducer::StopDataSource(DataSourceInstanceID ds_id) {
  auto meta_it = metatrace_.writers.find(ds_id);
  if (meta_it != metatrace_.writers.end()) {
    // Synchronously re-flush the metatrace writer to record more of the
    // teardown interactions, then ack the stop.
    meta_it->second.WriteAllAndFlushTraceWriter([] {});
    metatrace_.writers.erase(meta_it);
    endpoint_->NotifyDataSourceStopped(ds_id);
    return;
  }

  MaybeInitiateLazyStop(ds_id, &lazy_heapprofd_, kLazyHeapprofdPropertyName);
  MaybeInitiateLazyStop(ds_id, &lazy_traced_perf_, kLazyTracedPerfPropertyName);

  auto oome_it = java_hprof_oome_instances_.find(ds_id);
  if (oome_it != java_hprof_oome_instances_.end()) {
    java_hprof_oome_instances_.erase(oome_it);
    SetAndroidProperty(kJavaHprofOomActivePropertyName,
                       std::to_string(java_hprof_oome_instances_.size()));
  }
}

void BuiltinProducer::MaybeInitiateLazyStop(DataSourceInstanceID ds_id,
                                            LazyAndroidDaemonState* lazy_state,
                                            const char* prop_name) {
  auto lazy_it = lazy_state->instance_ids.find(ds_id);
  if (lazy_it != lazy_state->instance_ids.end()) {
    lazy_state->instance_ids.erase(lazy_it);

    // if no more sessions - stop daemon after a delay
    if (lazy_state->instance_ids.empty()) {
      uint64_t cur_generation = lazy_state->generation;
      auto weak_this = weak_factory_.GetWeakPtr();
      task_runner_->PostDelayedTask(
          [weak_this, cur_generation, lazy_state, prop_name] {
            if (!weak_this)
              return;
            // |lazy_state| should be valid if the |weak_this| is still valid
            if (lazy_state->generation == cur_generation)
              weak_this->SetAndroidProperty(prop_name, "");
          },
          lazy_state->stop_delay_ms);
    }
  }
}

void BuiltinProducer::Flush(FlushRequestID flush_id,
                            const DataSourceInstanceID* ds_ids,
                            size_t num_ds_ids,
                            FlushFlags) {
  for (size_t i = 0; i < num_ds_ids; i++) {
    auto meta_it = metatrace_.writers.find(ds_ids[i]);
    if (meta_it != metatrace_.writers.end()) {
      meta_it->second.WriteAllAndFlushTraceWriter([] {});
    }
    // nothing to be done for lazy sources
  }
  endpoint_->NotifyFlushComplete(flush_id);
}

bool BuiltinProducer::SetAndroidProperty(const std::string& name,
                                         const std::string& value) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  return __system_property_set(name.c_str(), value.c_str()) == 0;
#else
  // Allow this to be mocked out for tests on other platforms.
  base::ignore_result(name);
  base::ignore_result(value);
  return true;
#endif
}

}  // namespace perfetto
