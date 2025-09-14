/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/traced/probes/android_system_property/android_system_property_data_source.h"
#include <optional>

#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/android_utils.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/tracing/core/data_source_config.h"

#include "protos/perfetto/config/android/android_system_property_config.pbzero.h"
#include "protos/perfetto/trace/android/android_system_property.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

// static
const AndroidSystemPropertyDataSource::Descriptor
    AndroidSystemPropertyDataSource::descriptor = {
        /* name */ "android.system_property",
        /* flags */ Descriptor::kFlagsNone,
        /*fill_descriptor_func*/ nullptr,
};

constexpr const char* REQUIRED_NAME_PREFIX = "debug.tracing.";

AndroidSystemPropertyDataSource::AndroidSystemPropertyDataSource(
    base::TaskRunner* task_runner,
    const DataSourceConfig& ds_config,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {
  protos::pbzero::AndroidSystemPropertyConfig::Decoder cfg(
      ds_config.android_system_property_config_raw());
  poll_period_ms_ = cfg.poll_ms();
  if (poll_period_ms_ > 0 && poll_period_ms_ < 100) {
    PERFETTO_ILOG("poll_ms %" PRIu32
                  " is less than minimum of 100ms. Increasing to 100ms.",
                  poll_period_ms_);
    poll_period_ms_ = 100;
  }
  for (auto name_chars = cfg.property_name(); name_chars; ++name_chars) {
    auto name = (*name_chars).ToStdString();
    if (base::StartsWith(name, REQUIRED_NAME_PREFIX)) {
      property_names_.push_back(name);
    } else {
      PERFETTO_ELOG("Property %s lacks required prefix %s", name.c_str(),
                    REQUIRED_NAME_PREFIX);
    }
  }
}

void AndroidSystemPropertyDataSource::Start() {
  Tick();
}

base::WeakPtr<AndroidSystemPropertyDataSource>
AndroidSystemPropertyDataSource::GetWeakPtr() const {
  return weak_factory_.GetWeakPtr();
}

void AndroidSystemPropertyDataSource::Tick() {
  if (poll_period_ms_) {
    auto weak_this = GetWeakPtr();

    uint32_t delay_ms =
        poll_period_ms_ -
        static_cast<uint32_t>(base::GetWallTimeMs().count() % poll_period_ms_);
    task_runner_->PostDelayedTask(
        [weak_this]() -> void {
          if (weak_this) {
            weak_this->Tick();
          }
        },
        delay_ms);
  }
  WriteState();
}

void AndroidSystemPropertyDataSource::WriteState() {
  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  auto* properties = packet->set_android_system_property();
  for (const auto& name : property_names_) {
    const std::optional<std::string> value = ReadProperty(name);
    if (value) {
      auto* property = properties->add_values();
      property->set_name(name);
      property->set_value(*value);
    }
  }
  packet->Finalize();
  // For most data sources we would not want to flush every time we have
  // something to write. However this source tends to emit very slowly and it is
  // very possible that it would only flush at the end of the trace - at which
  // point it might not be able to write anything (e.g. DISCARD buffer might be
  // full). Taking the hit of 4kB each time we write seems reasonable to make
  // this behave more predictably.
  writer_->Flush();
}

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
const std::optional<std::string> AndroidSystemPropertyDataSource::ReadProperty(
    const std::string& name) {
  std::string value = base::GetAndroidProp(name.c_str());
  if (value.empty()) {
    PERFETTO_DLOG("Unable to read %s", name.c_str());
    return std::nullopt;
  }
  return std::make_optional(value);
}
#else
const std::optional<std::string> AndroidSystemPropertyDataSource::ReadProperty(
    const std::string&) {
  PERFETTO_ELOG("Android System Properties only supported on Android.");
  return std::nullopt;
}
#endif

void AndroidSystemPropertyDataSource::Flush(FlushRequestID,
                                            std::function<void()> callback) {
  writer_->Flush(callback);
}

}  // namespace perfetto
