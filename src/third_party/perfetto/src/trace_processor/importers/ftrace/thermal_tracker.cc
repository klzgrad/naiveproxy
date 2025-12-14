/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/ftrace/thermal_tracker.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/ftrace/thermal.pbzero.h"
#include "protos/perfetto/trace/ftrace/thermal_exynos.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {
namespace {

constexpr std::array<const char*, 7> kAcpmThermalZones = {
    "BIG", "MID", "LITTLE", "GPU", "ISP", "TPU", "AUR",
};

constexpr char kThermalZoneIdKey[] = "thermal_zone_id";

constexpr auto kThermalZoneDimension =
    tracks::StringDimensionBlueprint("thermal_zone");

constexpr auto kAcpmTemperatureTrackBlueprint = tracks::CounterBlueprint(
    "acpm_thermal_temperature",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(kThermalZoneDimension),
    tracks::FnNameBlueprint([](base::StringView zone) {
      return base::StackString<64>("%.*s Temperature",
                                   static_cast<int>(zone.size()), zone.data());
    }));

constexpr auto kAcpmCoolingTrackBlueprint = tracks::CounterBlueprint(
    "acpm_cooling_device_counter",
    tracks::UnknownUnitBlueprint(),
    tracks::DimensionBlueprints(kThermalZoneDimension),
    tracks::FnNameBlueprint([](base::StringView zone) {
      return base::StackString<64>("Tj-%.*s Cooling Device",
                                   static_cast<int>(zone.size()), zone.data());
    }));

}  // namespace

ThermalTracker::ThermalTracker(TraceProcessorContext* context)
    : context_(context) {}

void ThermalTracker::ParseThermalTemperature(int64_t timestamp,
                                             protozero::ConstBytes blob) {
  protos::pbzero::ThermalTemperatureFtraceEvent::Decoder event(blob);
  TrackId track = context_->track_tracker->InternTrack(
      tracks::kThermalTemperatureBlueprint,
      tracks::Dimensions(event.thermal_zone()));
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(event.temp()), track);
}

void ThermalTracker::ParseCdevUpdate(int64_t timestamp,
                                     protozero::ConstBytes blob) {
  protos::pbzero::CdevUpdateFtraceEvent::Decoder event(blob);
  TrackId track = context_->track_tracker->InternTrack(
      tracks::kCoolingDeviceCounterBlueprint, tracks::Dimensions(event.type()));
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(event.target()), track);
}

void ThermalTracker::ParseThermalExynosAcpmBulk(protozero::ConstBytes blob) {
  protos::pbzero::ThermalExynosAcpmBulkFtraceEvent::Decoder event(blob);
  auto tz_id = static_cast<uint32_t>(event.tz_id());
  if (tz_id >= kAcpmThermalZones.size()) {
    context_->storage->IncrementStats(
        stats::ftrace_thermal_exynos_acpm_unknown_tz_id);
    return;
  }
  auto timestamp = static_cast<int64_t>(event.timestamp());
  {
    TrackId track = context_->track_tracker->InternTrack(
        kAcpmTemperatureTrackBlueprint,
        tracks::Dimensions(kAcpmThermalZones[tz_id]));
    context_->event_tracker->PushCounter(
        timestamp, static_cast<double>(event.current_temp()), track,
        [this, tz_id](ArgsTracker::BoundInserter* inserter) {
          StringId key = context_->storage->InternString(kThermalZoneIdKey);
          inserter->AddArg(key, Variadic::Integer(tz_id));
        });
  }
  {
    TrackId track = context_->track_tracker->InternTrack(
        kAcpmCoolingTrackBlueprint,
        tracks::Dimensions(kAcpmThermalZones[tz_id]));
    context_->event_tracker->PushCounter(
        timestamp, static_cast<double>(event.cdev_state()), track,
        [this, tz_id](ArgsTracker::BoundInserter* inserter) {
          StringId key = context_->storage->InternString(kThermalZoneIdKey);
          inserter->AddArg(key, Variadic::Integer(tz_id));
        });
  }
}

void ThermalTracker::ParseThermalExynosAcpmHighOverhead(
    int64_t timestamp,
    protozero::ConstBytes blob) {
  protos::pbzero::ThermalExynosAcpmHighOverheadFtraceEvent::Decoder event(blob);
  auto tz_id = static_cast<uint32_t>(event.tz_id());
  if (tz_id >= kAcpmThermalZones.size()) {
    context_->storage->IncrementStats(
        stats::ftrace_thermal_exynos_acpm_unknown_tz_id);
    return;
  }
  {
    TrackId track = context_->track_tracker->InternTrack(
        kAcpmTemperatureTrackBlueprint,
        tracks::Dimensions(kAcpmThermalZones[tz_id]));
    context_->event_tracker->PushCounter(
        timestamp, static_cast<double>(event.current_temp()), track,
        [this, tz_id](ArgsTracker::BoundInserter* inserter) {
          StringId key = context_->storage->InternString(kThermalZoneIdKey);
          inserter->AddArg(key, Variadic::Integer(tz_id));
        });
  }
  {
    TrackId track = context_->track_tracker->InternTrack(
        kAcpmCoolingTrackBlueprint,
        tracks::Dimensions(kAcpmThermalZones[tz_id]));
    context_->event_tracker->PushCounter(
        timestamp, static_cast<double>(event.cdev_state()), track,
        [this, tz_id](ArgsTracker::BoundInserter* inserter) {
          StringId key = context_->storage->InternString(kThermalZoneIdKey);
          inserter->AddArg(key, Variadic::Integer(tz_id));
        });
  }
}

}  // namespace perfetto::trace_processor
