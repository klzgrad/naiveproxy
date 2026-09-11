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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_GLOBAL_STATS_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_GLOBAL_STATS_TRACKER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/utils.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"

namespace perfetto::trace_processor {

// Writer-side API for stats. The data lives on TraceStorage in the columnar
// StatsTable (see TraceStorage::stats_table()) — this class is a thin
// writer-side facade that routes Set/Increment/Get calls through the right
// (machine, trace) context based on each stat's declared Scope, and keeps a
// side index for O(1) row lookup on update.
class GlobalStatsTracker {
 public:
  using MachineId = tables::MachineTable::Id;
  using TraceId = tables::TraceFileTable::Id;

  explicit GlobalStatsTracker(TraceStorage* storage);

  void SetStats(std::optional<MachineId> machine_id,
                std::optional<TraceId> trace_id,
                size_t key,
                int64_t value);

  void IncrementStats(std::optional<MachineId> machine_id,
                      std::optional<TraceId> trace_id,
                      size_t key,
                      int64_t increment = 1);

  void SetIndexedStats(std::optional<MachineId> machine_id,
                       std::optional<TraceId> trace_id,
                       size_t key,
                       int index,
                       int64_t value);

  void IncrementIndexedStats(std::optional<MachineId> machine_id,
                             std::optional<TraceId> trace_id,
                             size_t key,
                             int index,
                             int64_t increment = 1);

  int64_t GetStats(std::optional<MachineId> machine_id,
                   std::optional<TraceId> trace_id,
                   size_t key) const;

  std::optional<int64_t> GetIndexedStats(std::optional<MachineId> machine_id,
                                         std::optional<TraceId> trace_id,
                                         size_t key,
                                         int index) const;

  // Convenience wrappers for stats with kGlobal scope. Each forwards to the
  // 5-arg variant with (nullopt, nullopt) and PERFETTO_CHECKs that the key's
  // scope really is kGlobal — calling these on a non-kGlobal stat is a
  // programmer error.
  void SetGlobalStats(size_t key, int64_t value);
  void IncrementGlobalStats(size_t key, int64_t increment = 1);
  int64_t GetGlobalStats(size_t key) const;

  void SetGlobalIndexedStats(size_t key, int index, int64_t value);
  void IncrementGlobalIndexedStats(size_t key,
                                   int index,
                                   int64_t increment = 1);
  std::optional<int64_t> GetGlobalIndexedStats(size_t key, int index) const;

  // Returns an RAII scope guard that, when destroyed, increments `key` by the
  // wall-time elapsed (in nanoseconds) since this call. `key` must be a
  // kGlobal stat.
  inline auto TraceExecutionTimeIntoStats(size_t key) {
    PERFETTO_CHECK(stats::kScopes[key] == stats::Scope::kGlobal);
    base::TimeNanos start = base::GetWallTimeNs();
    return base::OnScopeExit([this, key, start] {
      IncrementGlobalStats(key, (base::GetWallTimeNs() - start).count());
    });
  }

  // Eagerly emits a value=0 row for every kSingle stat of `scope`, scoped to
  // `(machine_id, trace_id)`. Must be called exactly once per (scope,
  // context) — typically from the owning tracker's constructor:
  //   * GlobalStatsTracker ctor zeroes kGlobal stats with (nullopt, nullopt).
  //   * StatsTracker ctor zeroes kMachineAndTrace stats with the context's
  //     (machine_id, trace_id).
  //
  // Why this exists: the legacy stats storage was a single
  // std::array<Stats, kNumKeys> default-constructed inside TraceStorage. As a
  // side effect, every kSingle stat was always visible in the `stats` SQL
  // view (and the JSON export's metadata.trace_processor_stats block) at
  // value=0 even if it was never written. Existing consumers depend on this:
  //   * metrics/sql/trace_stats.sql serializes every row in `stats`.
  //   * _stat_key_to_severity_and_name (prelude/after_eof/views.sql) does
  //     SELECT DISTINCT key, severity, name FROM stats ORDER BY key, joined
  //     against __intrinsic_trace_import_logs.stat_key — a missing stat row
  //     silently drops the join row.
  //   * export_json.cc writes one JSON key per kSingle stat (legacy
  //     contract: every key present, even at zero).
  //
  // In the new columnar StatsTable rows only exist when something is
  // written. Eagerly inserting at construction time preserves the legacy
  // contract regardless of whether any stat is ever touched. Cost:
  // O(kNumKeys) inserts per context (typically <300 rows per bucket).
  //
  // kIndexed stats are NOT pre-zeroed — the legacy IndexMap was sparse too
  // (default-empty), so absence in the new table matches.
  void ZeroSingleStatsForContext(stats::Scope scope,
                                 std::optional<MachineId> machine_id,
                                 std::optional<TraceId> trace_id);

 private:
  // Side-index entry: identifies a unique row in the StatsTable.
  struct StatsEntry {
    size_t key;
    std::optional<int> index;
    std::optional<MachineId> machine_id;
    std::optional<TraceId> trace_id;

    bool operator==(const StatsEntry& other) const {
      return key == other.key && index == other.index &&
             machine_id == other.machine_id && trace_id == other.trace_id;
    }

    template <typename H>
    friend H PerfettoHashValue(H h, const StatsEntry& value) {
      return H::Combine(std::move(h), value.key, value.index, value.machine_id,
                        value.trace_id);
    }
  };

  struct ContextIds {
    std::optional<MachineId> machine_id;
    std::optional<TraceId> trace_id;

    bool operator==(const ContextIds& other) const {
      return machine_id == other.machine_id && trace_id == other.trace_id;
    }

    template <typename H>
    friend H PerfettoHashValue(H h, const ContextIds& value) {
      return H::Combine(std::move(h), value.machine_id, value.trace_id);
    }
  };

  ContextIds GetContextIds(size_t key,
                           std::optional<MachineId> machine_id,
                           std::optional<TraceId> trace_id) const;

  // Returns the table row for an existing entry, or inserts a new row with
  // value=0 and registers it in the side index.
  tables::StatsTable::RowReference FindOrInsertRow(size_t key,
                                                   std::optional<int> index,
                                                   const ContextIds& ctx);

  TraceStorage* const storage_;

  // Pre-interned constant strings, keyed by stats:: enum values.
  std::array<StringId, stats::kNumKeys> name_ids_;
  std::array<StringId, stats::kNumKeys> description_ids_;
  std::array<StringId, stats::kNumSeverities> severity_ids_;
  std::array<StringId, stats::kNumSources> source_ids_;

  base::FlatHashMap<StatsEntry, tables::StatsTable::Id> id_by_entry_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_GLOBAL_STATS_TRACKER_H_
