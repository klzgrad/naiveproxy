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

#include "src/trace_processor/importers/proto/android_cpu_per_uid_module.h"
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
#include "src/trace_processor/importers/proto/android_cpu_per_uid_state.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/v8_module.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"

#include "protos/perfetto/trace/android/cpu_per_uid_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

using perfetto::protos::pbzero::TracePacket;

AndroidCpuPerUidModule::AndroidCpuPerUidModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context), context_(context) {
  RegisterForField(TracePacket::kCpuPerUidDataFieldNumber);
}

AndroidCpuPerUidModule::~AndroidCpuPerUidModule() = default;

void AndroidCpuPerUidModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData& packet,
    uint32_t field_id) {
  if (field_id != TracePacket::kCpuPerUidDataFieldNumber) {
    return;
  }

  auto* state = packet.sequence_state->GetCustomState<AndroidCpuPerUidState>();
  protos::pbzero::CpuPerUidData::Decoder evt(decoder.cpu_per_uid_data());

  if (evt.has_cluster_count()) {
    state->cluster_count = evt.cluster_count();
  }

  std::unordered_set<uint32_t> uid_with_value_this_packet;

  bool parse_error = false;
  uint32_t cluster = 0;
  auto uid_it = evt.uid(&parse_error);
  for (auto time_it = evt.total_time_ms(&parse_error); uid_it && time_it;
       ++time_it) {
    uid_with_value_this_packet.insert(*uid_it);
    uint64_t key = ((uint64_t(*uid_it)) << 32) | cluster;
    uint64_t* previous = state->last_values.Find(key);
    uint64_t time_ms;
    if (previous) {
      time_ms = *time_it + *previous;
      *previous = time_ms;
    } else {
      time_ms = *time_it;
      state->last_values.Insert(key, time_ms);
    }

    UpdateCounter(ts, *uid_it, cluster, time_ms);

    cluster++;
    if (cluster >= state->cluster_count) {
      cluster = 0;
      uid_it++;
    }
  }

  // Anything we knew about but didn't see in this packet must not have
  // incremented.
  for (auto it = state->last_values.GetIterator(); it; ++it) {
    uint32_t uid = it.key() >> 32;
    if (uid_with_value_this_packet.count(uid)) {
      continue;
    }
    uint32_t cluster_id = it.key() & 0xffffffff;

    UpdateCounter(ts, uid, cluster_id, it.value());
  }
}

void AndroidCpuPerUidModule::UpdateCounter(int64_t ts,
                                           uint32_t uid,
                                           uint32_t cluster,
                                           uint64_t value) {
  static constexpr auto kBlueprint = tracks::CounterBlueprint(
      "android_cpu_per_uid", tracks::StaticUnitBlueprint("ms"),
      tracks::DimensionBlueprints(tracks::UintDimensionBlueprint("uid"),
                                  tracks::UintDimensionBlueprint("cluster")),
      tracks::FnNameBlueprint([](uint32_t uid, uint32_t cluster) {
        return base::StackString<1024>("CPU for UID %d CL%d", uid, cluster);
      }));

  TrackId track = context_->track_tracker->InternTrack(
      kBlueprint, tracks::Dimensions(uid, cluster));
  context_->event_tracker->PushCounter(ts, double(value), track);
}

}  // namespace perfetto::trace_processor
