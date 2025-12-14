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

#include "src/traced/probes/initial_display_state/initial_display_state_data_source.h"
#include <optional>

#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/android_utils.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/tracing/core/data_source_config.h"

#include "protos/perfetto/config/android/android_polled_state_config.pbzero.h"
#include "protos/perfetto/trace/android/initial_display_state.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

// static
const InitialDisplayStateDataSource::Descriptor
    InitialDisplayStateDataSource::descriptor = {
        /* name */ "android.polled_state",
        /* flags */ Descriptor::kFlagsNone,
        /*fill_descriptor_func*/ nullptr,
};

InitialDisplayStateDataSource::InitialDisplayStateDataSource(
    base::TaskRunner* task_runner,
    const DataSourceConfig& ds_config,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {
  protos::pbzero::AndroidPolledStateConfig::Decoder cfg(
      ds_config.android_polled_state_config_raw());
  poll_period_ms_ = cfg.poll_ms();
  if (poll_period_ms_ > 0 && poll_period_ms_ < 100) {
    PERFETTO_ILOG("poll_ms %" PRIu32
                  " is less than minimum of 100ms. Increasing to 100ms.",
                  poll_period_ms_);
    poll_period_ms_ = 100;
  }
}

void InitialDisplayStateDataSource::Start() {
  Tick();
}

base::WeakPtr<InitialDisplayStateDataSource>
InitialDisplayStateDataSource::GetWeakPtr() const {
  return weak_factory_.GetWeakPtr();
}

void InitialDisplayStateDataSource::Tick() {
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

void InitialDisplayStateDataSource::WriteState() {
  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  const std::optional<std::string> screen_state_str =
      ReadProperty("debug.tracing.screen_state");
  const std::optional<std::string> screen_brightness_str =
      ReadProperty("debug.tracing.screen_brightness");
  const std::optional<int> screen_state =
      screen_state_str ? base::StringToInt32(*screen_state_str) : std::nullopt;
  const std::optional<double> screen_brightness =
      screen_brightness_str ? base::StringToDouble(*screen_brightness_str)
                            : std::nullopt;
  if (screen_state || screen_brightness) {
    auto* state = packet->set_initial_display_state();
    if (screen_state) {
      state->set_display_state(*screen_state);
    }
    if (screen_brightness) {
      state->set_brightness(*screen_brightness);
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
const std::optional<std::string> InitialDisplayStateDataSource::ReadProperty(
    const std::string name) {
  std::string value = base::GetAndroidProp(name.c_str());
  if (value.empty()) {
    PERFETTO_ELOG("Unable to read %s", name.c_str());
    return std::nullopt;
  }
  return std::make_optional(value);
}
#else
const std::optional<std::string> InitialDisplayStateDataSource::ReadProperty(
    const std::string name __attribute__((unused))) {
  PERFETTO_ELOG("Initial display state only supported on Android.");
  return std::nullopt;
}
#endif

void InitialDisplayStateDataSource::Flush(FlushRequestID,
                                          std::function<void()> callback) {
  writer_->Flush(callback);
}

}  // namespace perfetto
