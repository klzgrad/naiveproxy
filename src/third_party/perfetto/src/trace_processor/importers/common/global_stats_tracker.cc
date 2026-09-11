/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/importers/common/global_stats_tracker.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "perfetto/base/logging.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

GlobalStatsTracker::GlobalStatsTracker(TraceStorage* storage)
    : storage_(storage) {
  for (size_t i = 0; i < stats::kNumKeys; ++i) {
    name_ids_[i] = storage->InternString(stats::kNames[i]);
    description_ids_[i] = storage->InternString(stats::kDescriptions[i]);
  }
  severity_ids_[stats::kInfo] = storage->InternString("info");
  severity_ids_[stats::kNotice] = storage->InternString("notice");
  severity_ids_[stats::kDataLoss] = storage->InternString("data_loss");
  severity_ids_[stats::kError] = storage->InternString("error");
  source_ids_[stats::kTrace] = storage->InternString("trace");
  source_ids_[stats::kAnalysis] = storage->InternString("analysis");

  // Pre-emit value=0 rows for every kGlobal kSingle stat so they are visible
  // in SQL/JSON regardless of whether anything ever writes them. See
  // ZeroSingleStatsForContext for the rationale.
  ZeroSingleStatsForContext(stats::Scope::kGlobal, std::nullopt, std::nullopt);
}

void GlobalStatsTracker::SetStats(std::optional<MachineId> machine_id,
                                  std::optional<TraceId> trace_id,
                                  size_t key,
                                  int64_t value) {
  PERFETTO_CHECK(key < stats::kNumKeys);
  PERFETTO_CHECK(stats::kTypes[key] == stats::kSingle);
  auto ctx = GetContextIds(key, machine_id, trace_id);
  FindOrInsertRow(key, std::nullopt, ctx).set_value(value);
}

void GlobalStatsTracker::IncrementStats(std::optional<MachineId> machine_id,
                                        std::optional<TraceId> trace_id,
                                        size_t key,
                                        int64_t increment) {
  PERFETTO_CHECK(key < stats::kNumKeys);
  PERFETTO_CHECK(stats::kTypes[key] == stats::kSingle);
  auto ctx = GetContextIds(key, machine_id, trace_id);
  auto rr = FindOrInsertRow(key, std::nullopt, ctx);
  rr.set_value(rr.value() + increment);
}

void GlobalStatsTracker::SetIndexedStats(std::optional<MachineId> machine_id,
                                         std::optional<TraceId> trace_id,
                                         size_t key,
                                         int index,
                                         int64_t value) {
  PERFETTO_CHECK(key < stats::kNumKeys);
  PERFETTO_CHECK(stats::kTypes[key] == stats::kIndexed);
  auto ctx = GetContextIds(key, machine_id, trace_id);
  FindOrInsertRow(key, index, ctx).set_value(value);
}

void GlobalStatsTracker::IncrementIndexedStats(
    std::optional<MachineId> machine_id,
    std::optional<TraceId> trace_id,
    size_t key,
    int index,
    int64_t increment) {
  PERFETTO_CHECK(key < stats::kNumKeys);
  PERFETTO_CHECK(stats::kTypes[key] == stats::kIndexed);
  auto ctx = GetContextIds(key, machine_id, trace_id);
  auto rr = FindOrInsertRow(key, index, ctx);
  rr.set_value(rr.value() + increment);
}

int64_t GlobalStatsTracker::GetStats(std::optional<MachineId> machine_id,
                                     std::optional<TraceId> trace_id,
                                     size_t key) const {
  PERFETTO_CHECK(key < stats::kNumKeys);
  PERFETTO_CHECK(stats::kTypes[key] == stats::kSingle);
  auto ctx = GetContextIds(key, machine_id, trace_id);
  StatsEntry entry{key, std::nullopt, ctx.machine_id, ctx.trace_id};
  if (const auto* id_ptr = id_by_entry_.Find(entry)) {
    return storage_->stats_table()[*id_ptr].value();
  }
  return 0;
}

std::optional<int64_t> GlobalStatsTracker::GetIndexedStats(
    std::optional<MachineId> machine_id,
    std::optional<TraceId> trace_id,
    size_t key,
    int index) const {
  PERFETTO_CHECK(key < stats::kNumKeys);
  PERFETTO_CHECK(stats::kTypes[key] == stats::kIndexed);
  auto ctx = GetContextIds(key, machine_id, trace_id);
  StatsEntry entry{key, index, ctx.machine_id, ctx.trace_id};
  if (const auto* id_ptr = id_by_entry_.Find(entry)) {
    return storage_->stats_table()[*id_ptr].value();
  }
  return std::nullopt;
}

void GlobalStatsTracker::SetGlobalStats(size_t key, int64_t value) {
  PERFETTO_CHECK(stats::kScopes[key] == stats::Scope::kGlobal);
  SetStats(std::nullopt, std::nullopt, key, value);
}

void GlobalStatsTracker::IncrementGlobalStats(size_t key, int64_t increment) {
  PERFETTO_CHECK(stats::kScopes[key] == stats::Scope::kGlobal);
  IncrementStats(std::nullopt, std::nullopt, key, increment);
}

int64_t GlobalStatsTracker::GetGlobalStats(size_t key) const {
  PERFETTO_CHECK(stats::kScopes[key] == stats::Scope::kGlobal);
  return GetStats(std::nullopt, std::nullopt, key);
}

void GlobalStatsTracker::SetGlobalIndexedStats(size_t key,
                                               int index,
                                               int64_t value) {
  PERFETTO_CHECK(stats::kScopes[key] == stats::Scope::kGlobal);
  SetIndexedStats(std::nullopt, std::nullopt, key, index, value);
}

void GlobalStatsTracker::IncrementGlobalIndexedStats(size_t key,
                                                     int index,
                                                     int64_t increment) {
  PERFETTO_CHECK(stats::kScopes[key] == stats::Scope::kGlobal);
  IncrementIndexedStats(std::nullopt, std::nullopt, key, index, increment);
}

std::optional<int64_t> GlobalStatsTracker::GetGlobalIndexedStats(
    size_t key,
    int index) const {
  PERFETTO_CHECK(stats::kScopes[key] == stats::Scope::kGlobal);
  return GetIndexedStats(std::nullopt, std::nullopt, key, index);
}

GlobalStatsTracker::ContextIds GlobalStatsTracker::GetContextIds(
    size_t key,
    std::optional<MachineId> machine_id,
    std::optional<TraceId> trace_id) const {
  switch (stats::kScopes[key]) {
    case stats::Scope::kGlobal:
      return {std::nullopt, std::nullopt};
    case stats::Scope::kMachine:
      PERFETTO_CHECK(machine_id.has_value());
      return {machine_id, std::nullopt};
    case stats::Scope::kTrace:
      PERFETTO_CHECK(trace_id.has_value());
      return {std::nullopt, trace_id};
    case stats::Scope::kMachineAndTrace:
      PERFETTO_CHECK(machine_id.has_value());
      PERFETTO_CHECK(trace_id.has_value());
      return {machine_id, trace_id};
    case stats::Scope::kNumScopes:
      PERFETTO_FATAL("Invalid scope");
  }
  PERFETTO_FATAL("For GCC");
}

tables::StatsTable::RowReference GlobalStatsTracker::FindOrInsertRow(
    size_t key,
    std::optional<int> index,
    const ContextIds& ctx) {
  auto& table = *storage_->mutable_stats_table();
  StatsEntry entry{key, index, ctx.machine_id, ctx.trace_id};
  if (auto* id_ptr = id_by_entry_.Find(entry)) {
    return table[*id_ptr];
  }
  tables::StatsTable::Row row;
  row.name = name_ids_[key];
  row.key = static_cast<int64_t>(key);
  row.idx = index ? std::make_optional<int64_t>(*index) : std::nullopt;
  row.severity = severity_ids_[stats::kSeverities[key]];
  row.source = source_ids_[stats::kSources[key]];
  row.value = 0;
  row.description = description_ids_[key];
  row.machine_id = ctx.machine_id;
  row.trace_id = ctx.trace_id;
  auto id_and_row = table.Insert(row);
  id_by_entry_.Insert(entry, id_and_row.id);
  return table[id_and_row.row];
}

void GlobalStatsTracker::ZeroSingleStatsForContext(
    stats::Scope scope,
    std::optional<MachineId> machine_id,
    std::optional<TraceId> trace_id) {
  auto& table = *storage_->mutable_stats_table();
  for (size_t k = 0; k < stats::kNumKeys; ++k) {
    if (stats::kScopes[k] != scope) {
      continue;
    }
    if (stats::kTypes[k] != stats::kSingle) {
      continue;
    }
    tables::StatsTable::Row row;
    row.name = name_ids_[k];
    row.key = static_cast<int64_t>(k);
    row.idx = std::nullopt;
    row.severity = severity_ids_[stats::kSeverities[k]];
    row.source = source_ids_[stats::kSources[k]];
    row.value = 0;
    row.description = description_ids_[k];
    row.machine_id = machine_id;
    row.trace_id = trace_id;
    auto id_and_row = table.Insert(row);
    id_by_entry_.Insert(StatsEntry{k, std::nullopt, machine_id, trace_id},
                        id_and_row.id);
  }
}

}  // namespace perfetto::trace_processor
