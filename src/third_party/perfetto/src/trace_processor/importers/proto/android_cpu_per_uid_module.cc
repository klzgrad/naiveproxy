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
#include "src/trace_processor/importers/common/sparse_counter_tracker.h"
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

std::pair<uint32_t, uint32_t> SplitKey(uint64_t key) {
  uint32_t uid = key >> 32;
  uint32_t cluster = key & 0xffffffff;
  return std::make_pair(uid, cluster);
}

// Returns the package ID for a uid (the uid minus the user portion).
uint32_t PkgId(uint32_t uid) {
  return uid % 100000;
}

// Returns whether the UID is part of an anonymous UID range, such as the shared
// uid range, or isolated UID range.
bool IsGroupedUid(uint32_t uid) {
  uint32_t pkgid = PkgId(uid);
  return (50000 <= pkgid && pkgid < 60000) ||
         (90000 <= pkgid && pkgid < 100000);
}

// Returns the canonical grouped UID for UIDs within anonymous ranges. For
// example 1090123 is mapped to 1090000.
uint32_t GetGroupedUid(uint32_t uid) {
  if (IsGroupedUid(uid)) {
    return uid - (uid % 10000);
  }
  return uid;
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

ModuleResult AndroidCpuPerUidModule::TokenizePacket(
    const TokenizePacketArgs& args) {
  if (args.field.id() != TracePacket::kCpuPerUidDataFieldNumber) {
    return ModuleResult::Ignored();
  }

  auto* state = args.state->GetCustomState<AndroidCpuPerUidState>();
  protos::pbzero::CpuPerUidData::Decoder evt(
      args.field.Cast<TracePacket::kCpuPerUidData>());

  if (evt.has_cluster_count()) {
    state->cluster_count = evt.cluster_count();
  }

  bool parse_error = false;
  uint32_t cluster = 0;
  auto uid_it = evt.uid(&parse_error);
  for (auto time_it = evt.total_time_ms(&parse_error); uid_it && time_it;
       ++time_it) {
    auto [time_ms, delta_ms] =
        IncrementalStateUpdate(*uid_it, cluster, *time_it, state);

    // Totals are computed using grouped IDs, because totals correspond 1:1 with
    // real tracks. Grouped UIDs (such as isolated UIDs) are consolidated onto
    // a single track for the group.
    uint32_t grouped_uid = GetGroupedUid(*uid_it);
    ComputeTotals(grouped_uid, cluster, delta_ms);

    cluster++;
    if (cluster >= state->cluster_count) {
      cluster = 0;
      uid_it++;
    }
  }

  for (auto it = system_totals_.GetIterator(); it; ++it) {
    UpdateTotals(args.ts, "System", it.key(), it.value());
  }
  for (auto it = app_totals_.GetIterator(); it; ++it) {
    UpdateTotals(args.ts, "Apps", it.key(), it.value());
  }
  for (auto it = cumulative_.GetIterator(); it; ++it) {
    auto [uid, cluster_id] = SplitKey(it.key());
    if (IsGroupedUid(uid)) {
      UpdateCounter(args.ts, uid, cluster_id, it.value());
    }
  }
  for (auto it = state->last_values.GetIterator(); it; ++it) {
    auto [uid, cluster_id] = SplitKey(it.key());
    if (!IsGroupedUid(uid)) {
      UpdateCounter(args.ts, uid, cluster_id, it.value());
    }
  }

  return ModuleResult::Handled();
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
                                           uint64_t delta_ms) {
  uint64_t key = MakeKey(uid, cluster);
  cumulative_[key] += delta_ms;
  if (PkgId(uid) < 10000) {
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
  context_->sparse_counter_tracker->PushCounter(ts, track, double(value));
}

void AndroidCpuPerUidModule::UpdateTotals(int64_t ts,
                                          base::StringView name,
                                          uint32_t cluster,
                                          uint64_t value) {
  TrackId track = context_->track_tracker->InternTrack(
      kCpuTotalsBlueprint, tracks::Dimensions(name, cluster));
  context_->sparse_counter_tracker->PushCounter(ts, track, double(value));
}

std::pair<uint64_t, uint64_t> AndroidCpuPerUidModule::IncrementalStateUpdate(
    uint32_t uid,
    uint32_t cluster,
    uint64_t raw_time,
    AndroidCpuPerUidState* state) {
  uint64_t key = MakeKey(uid, cluster);
  uint64_t delta_ms = 0;

  // The meaning of the raw_time depends on incremental state. The first time
  // we see a key, it's the absolute value of the counter since boot. For all
  // subsequent times, it's the difference from the last.
  auto [incr_value, incr_inserted] = state->last_values.Insert(key, raw_time);
  if (!incr_inserted) {
    *incr_value += raw_time;
  }

  // The last value irregardless of incremental state is stored on the importer.
  // We use this to compute the non-negative delta since the last value.
  auto [global_value, _] = last_value_.Insert(key, *incr_value);
  if (*incr_value > *global_value) {
    delta_ms = *incr_value - *global_value;
  }

  *global_value = *incr_value;
  return std::make_pair(*incr_value, delta_ms);
}

}  // namespace perfetto::trace_processor
