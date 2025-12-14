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

#include "src/trace_processor/importers/proto/android_kernel_wakelocks_module.h"
#include <cstdint>

#include <string>
#include <unordered_set>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/protozero/field.h"
#include "src/kernel_utils/kernel_wakelock_errors.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/proto/android_kernel_wakelocks_state.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/v8_module.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"

#include "protos/perfetto/trace/android/kernel_wakelock_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

using perfetto::protos::pbzero::TracePacket;

AndroidKernelWakelocksModule::AndroidKernelWakelocksModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      context_(context),
      kernel_name_id_(context->storage->InternString("kernel")),
      native_name_id_(context->storage->InternString("native")),
      unknown_name_id_(context->storage->InternString("unknown")) {
  RegisterForField(TracePacket::kKernelWakelockDataFieldNumber);
}

AndroidKernelWakelocksModule::~AndroidKernelWakelocksModule() = default;

void AndroidKernelWakelocksModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData& packet,
    uint32_t field_id) {
  if (field_id != TracePacket::kKernelWakelockDataFieldNumber) {
    return;
  }

  std::unordered_set<std::string> names_with_value_this_packet;

  auto* state =
      packet.sequence_state->GetCustomState<AndroidKernelWakelockState>();
  protos::pbzero::KernelWakelockData::Decoder evt(
      decoder.kernel_wakelock_data());
  for (auto it = evt.wakelock(); it; ++it) {
    protos::pbzero::KernelWakelockData::Wakelock::Decoder wakelock(*it);
    std::string name = wakelock.wakelock_name().ToStdString();
    auto [info, inserted] = state->wakelocks.Insert(
        wakelock.wakelock_id(), AndroidKernelWakelockState::Metadata{});
    if (!inserted) {
      context_->storage->IncrementStats(stats::kernel_wakelock_reused_id);
      continue;
    }
    info->name = name;
    info->type =
        static_cast<protos::pbzero::KernelWakelockData::Wakelock::Type>(
            wakelock.wakelock_type());
  }

  bool parse_error = false;
  auto time_it = evt.time_held_millis(&parse_error);
  for (auto it = evt.wakelock_id(&parse_error); it && time_it;
       ++it, ++time_it) {
    auto* data = state->wakelocks.Find(*it);
    if (!data) {
      context_->storage->IncrementStats(stats::kernel_wakelock_unknown_id);
      continue;
    }

    const auto& name = data->name;
    names_with_value_this_packet.insert(name);

    uint64_t delta = *time_it;
    auto [last_value, inserted] = state->wakelock_last_values.Insert(
        name, AndroidKernelWakelockState::LastValue{});
    last_value->value += delta;
    last_value->type = data->type;
    UpdateCounter(ts, name, data->type, last_value->value);
  }

  uint64_t traced_errors = evt.error_flags();
  if (traced_errors & kKernelWakelockErrorZeroValue) {
    context_->storage->IncrementStats(
        stats::kernel_wakelock_zero_value_reported);
  }
  if (traced_errors & kKernelWakelockErrorNonMonotonicValue) {
    context_->storage->IncrementStats(
        stats::kernel_wakelock_non_monotonic_value_reported);
  }
  if (traced_errors & kKernelWakelockErrorImplausiblyLargeValue) {
    context_->storage->IncrementStats(
        stats::kernel_wakelock_implausibly_large_value_reported);
  }

  // Anything we knew about but didn't see in this packet must not have
  // incremented.
  for (auto it = state->wakelock_last_values.GetIterator(); it; ++it) {
    if (names_with_value_this_packet.count(it.key())) {
      continue;
    }
    UpdateCounter(ts, it.key(), it.value().type, it.value().value);
  }
}

void AndroidKernelWakelocksModule::UpdateCounter(
    int64_t ts,
    const std::string& name,
    protos::pbzero::KernelWakelockData_Wakelock_Type type,
    uint64_t value) {
  static constexpr auto kBlueprint = tracks::CounterBlueprint(
      "android_kernel_wakelock", tracks::StaticUnitBlueprint("ms"),
      tracks::DimensionBlueprints(
          tracks::StringDimensionBlueprint("wakelock_name"),
          tracks::StringDimensionBlueprint("wakelock_type")),
      tracks::DynamicNameBlueprint());
  StringId type_id;
  switch (type) {
    case protos::pbzero::KernelWakelockData_Wakelock_Type::WAKELOCK_TYPE_KERNEL:
      type_id = kernel_name_id_;
      break;
    case protos::pbzero::KernelWakelockData_Wakelock_Type::WAKELOCK_TYPE_NATIVE:
      type_id = native_name_id_;
      break;
    case protos::pbzero::KernelWakelockData_Wakelock_Type::
        WAKELOCK_TYPE_UNKNOWN:
      type_id = unknown_name_id_;
      break;
  }

  StringId name_id = context_->storage->InternString(name);
  TrackId track = context_->track_tracker->InternTrack(
      kBlueprint,
      tracks::Dimensions(context_->storage->GetString(name_id),
                         context_->storage->GetString(type_id)),
      tracks::DynamicName(name_id));
  context_->event_tracker->PushCounter(ts, 1e6 * double(value), track);
}

}  // namespace perfetto::trace_processor
