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

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/proto/android_cpu_per_uid_state.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/v8_module.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/android_tables_py.h"

#include "protos/perfetto/trace/android/cpu_per_uid_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {
namespace {
uint64_t MakeKey(uint32_t uid, uint32_t cluster) {
  return ((uint64_t(uid)) << 32) | cluster;
}

constexpr auto kCpuPerUidBlueprint = tracks::CounterBlueprint(
    "android_cpu_per_uid",
    tracks::StaticUnitBlueprint("ms"),
    tracks::DimensionBlueprints(tracks::kUidDimensionBlueprint,
                                tracks::UintDimensionBlueprint("cluster")),
    tracks::FnNameBlueprint([](uint32_t uid, uint32_t cluster) {
      return base::StackString<128>("CPU for UID %u CL%u", uid, cluster);
    }));

constexpr auto kCpuTotalsBlueprint = tracks::CounterBlueprint(
    "android_cpu_per_uid_totals",
    tracks::StaticUnitBlueprint("ms"),
    // TODO(lalitm): allow FnNameBlueprint and StringIdDimensionBlueprint to
    // work together.
    tracks::DimensionBlueprints(tracks::StringDimensionBlueprint("type"),
                                tracks::UintDimensionBlueprint("cluster")),
    tracks::FnNameBlueprint([](base::StringView type, uint32_t cluster) {
      return base::StackString<128>("CPU for %.*s CL%u",
                                    static_cast<int>(type.size()), type.data(),
                                    cluster);
    }));

}  // namespace

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
    uint64_t key = MakeKey(*uid_it, cluster);
    uint64_t* previous = state->last_values.Find(key);
    uint64_t time_ms;
    if (previous) {
      time_ms = *time_it + *previous;
      *previous = time_ms;
    } else {
      time_ms = *time_it;
      state->last_values.Insert(key, time_ms);
    }

    ComputeTotals(*uid_it, cluster, time_ms);
    UpdateCounter(ts, *uid_it, cluster, time_ms);
    cluster++;
    if (cluster >= state->cluster_count) {
      cluster = 0;
      uid_it++;
    }
  }

  for (auto it = system_totals_.GetIterator(); it; ++it) {
    UpdateTotals(ts, "System", it.key(), it.value());
  }
  for (auto it = app_totals_.GetIterator(); it; ++it) {
    UpdateTotals(ts, "Apps", it.key(), it.value());
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

void AndroidCpuPerUidModule::OnEventsFullyExtracted() {
  std::vector<tables::AndroidCpuPerUidTrackTable::Row> rows;
  rows.reserve(cumulative_.size());
  for (auto it = cumulative_.GetIterator(); it; ++it) {
    tables::AndroidCpuPerUidTrackTable::Row row;
    row.uid = it.key() >> 32;
    row.cluster = it.key() & 0xffffffff;
    row.total_cpu_millis = static_cast<int64_t>(it.value());
    row.track_id = context_->track_tracker->InternTrack(
        kCpuPerUidBlueprint, tracks::Dimensions(row.uid, row.cluster));
    rows.push_back(row);
  }

  std::sort(rows.begin(), rows.end(),
            [](auto& a, auto& b) { return a.track_id < b.track_id; });

  for (const auto& row : rows) {
    context_->storage->mutable_android_cpu_per_uid_track_table()->Insert(row);
  }
}

void AndroidCpuPerUidModule::ComputeTotals(uint32_t uid,
                                           uint32_t cluster,
                                           uint64_t time_ms) {
  // Note: in ParseTracePacketData, previous is computed per intern sequence,
  // whereas here it's computed globally post-interning.
  uint64_t key = MakeKey(uid, cluster);
  auto [previous, inserted] = last_value_.Insert(key, time_ms);

  uint64_t delta_ms = 0;
  if (time_ms > *previous && !inserted) {
    delta_ms = time_ms - *previous;
  }
  *previous = time_ms;

  cumulative_[key] += delta_ms;
  if ((uid % 100000) < 10000) {
    system_totals_[cluster] += delta_ms;
  } else {
    app_totals_[cluster] += delta_ms;
  }
}

void AndroidCpuPerUidModule::UpdateCounter(int64_t ts,
                                           uint32_t uid,
                                           uint32_t cluster,
                                           uint64_t value) {
  TrackId track = context_->track_tracker->InternTrack(
      kCpuPerUidBlueprint, tracks::Dimensions(uid, cluster));
  context_->event_tracker->PushCounter(ts, double(value), track);
}

void AndroidCpuPerUidModule::UpdateTotals(int64_t ts,
                                          base::StringView name,
                                          uint32_t cluster,
                                          uint64_t value) {
  TrackId track = context_->track_tracker->InternTrack(
      kCpuTotalsBlueprint, tracks::Dimensions(name, cluster));
  context_->event_tracker->PushCounter(ts, double(value), track);
}

}  // namespace perfetto::trace_processor
