/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/flamegraph_construction_algorithms.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"

namespace perfetto::trace_processor {

namespace {
struct MergedCallsite {
  StringId frame_name;
  StringId mapping_name;
  std::optional<StringId> source_file;
  std::optional<uint32_t> line_number;
  std::optional<uint32_t> parent_idx;
  bool operator<(const MergedCallsite& o) const {
    return std::tie(frame_name, mapping_name, parent_idx) <
           std::tie(o.frame_name, o.mapping_name, o.parent_idx);
  }
};

struct FlamegraphTableAndMergedCallsites {
  std::unique_ptr<tables::ExperimentalFlamegraphTable> tbl;
  std::vector<uint32_t> callsite_to_merged_callsite;
};

std::vector<MergedCallsite> GetMergedCallsites(TraceStorage* storage,
                                               uint32_t callstack_row) {
  const tables::StackProfileCallsiteTable& callsites_tbl =
      storage->stack_profile_callsite_table();
  const tables::StackProfileFrameTable& frames_tbl =
      storage->stack_profile_frame_table();
  const tables::SymbolTable& symbols_tbl = storage->symbol_table();
  const tables::StackProfileMappingTable& mapping_tbl =
      storage->stack_profile_mapping_table();

  auto frame = frames_tbl.FindById(callsites_tbl[callstack_row].frame_id());
  StringId mapping_name = mapping_tbl.FindById(frame->mapping())->name();
  std::optional<uint32_t> symbol_set_id = frame->symbol_set_id();

  if (!symbol_set_id) {
    StringId frame_name = frame->name();
    std::optional<StringId> deobfuscated_name = frame->deobfuscated_name();
    return {{deobfuscated_name ? *deobfuscated_name : frame_name, mapping_name,
             std::nullopt, std::nullopt, std::nullopt}};
  }

  std::vector<MergedCallsite> result;
  // id == symbol_set_id for the bottommost frame.
  // TODO(lalitm): Encode this optimization in the table and remove this
  // custom optimization.
  uint32_t symbol_set_idx = symbols_tbl.FindById(SymbolId(*symbol_set_id))
                                ->ToRowNumber()
                                .row_number();
  for (uint32_t i = symbol_set_idx;
       i < symbols_tbl.row_count() &&
       symbols_tbl[i].symbol_set_id() == *symbol_set_id;
       ++i) {
    result.emplace_back(MergedCallsite{
        symbols_tbl[i].name(), mapping_name, symbols_tbl[i].source_file(),
        symbols_tbl[i].line_number(), std::nullopt});
  }
  std::reverse(result.begin(), result.end());
  return result;
}
}  // namespace

static FlamegraphTableAndMergedCallsites BuildFlamegraphTableTreeStructure(
    TraceStorage* storage,
    std::optional<UniquePid> upid,
    std::optional<std::string> upid_group,
    int64_t default_timestamp,
    StringId profile_type) {
  const tables::StackProfileCallsiteTable& callsites_tbl =
      storage->stack_profile_callsite_table();

  std::vector<uint32_t> callsite_to_merged_callsite(callsites_tbl.row_count(),
                                                    0);
  std::map<MergedCallsite, uint32_t> merged_callsites_to_table_idx;

  std::unique_ptr<tables::ExperimentalFlamegraphTable> tbl(
      new tables::ExperimentalFlamegraphTable(storage->mutable_string_pool()));

  // FORWARD PASS:
  // Aggregate callstacks by frame name / mapping name. Use symbolization
  // data.
  for (uint32_t i = 0; i < callsites_tbl.row_count(); ++i) {
    std::optional<uint32_t> parent_idx;

    auto opt_parent_id = callsites_tbl[i].parent_id();
    if (opt_parent_id) {
      parent_idx =
          callsites_tbl.FindById(*opt_parent_id)->ToRowNumber().row_number();
      // Make sure what we index into has been populated already.
      PERFETTO_CHECK(*parent_idx < i);
      parent_idx = callsite_to_merged_callsite[*parent_idx];
    }

    auto callsites = GetMergedCallsites(storage, i);
    // Loop below needs to run at least once for parent_idx to get updated.
    PERFETTO_CHECK(!callsites.empty());
    std::map<MergedCallsite, uint32_t> callsites_to_rowid;
    for (MergedCallsite& merged_callsite : callsites) {
      merged_callsite.parent_idx = parent_idx;
      auto it = merged_callsites_to_table_idx.find(merged_callsite);
      if (it == merged_callsites_to_table_idx.end()) {
        std::tie(it, std::ignore) = merged_callsites_to_table_idx.emplace(
            merged_callsite, merged_callsites_to_table_idx.size());
        tables::ExperimentalFlamegraphTable::Row row{};
        if (parent_idx) {
          row.depth = (*tbl)[*parent_idx].depth() + 1;
          row.parent_id = (*tbl)[*parent_idx].id();
        } else {
          row.depth = 0;
          row.parent_id = std::nullopt;
        }

        // The 'ts' column is given a default value, taken from the query.
        // So if the query is:
        // `select * from experimental_flamegraph(
        //   'native',
        //   605908369259172,
        //   NULL,
        //   1,
        //   NULL,
        //   NULL
        // )`
        // then row.ts == 605908369259172, for all rows
        // This is not accurate. However, at present there is no other
        // straightforward way of assigning timestamps to non-leaf nodes in the
        // flamegraph tree. Non-leaf nodes would have to be assigned >= 1
        // timestamps, which would increase data size without an advantage.
        row.ts = default_timestamp;
        if (upid) {
          row.upid = *upid;
        }
        if (upid_group) {
          row.upid_group = storage->InternString(base::StringView(*upid_group));
        }
        row.profile_type = profile_type;
        row.name = merged_callsite.frame_name;
        row.map_name = merged_callsite.mapping_name;
        tbl->Insert(row);
        callsites_to_rowid[merged_callsite] =
            static_cast<uint32_t>(merged_callsites_to_table_idx.size() - 1);

        PERFETTO_CHECK(merged_callsites_to_table_idx.size() ==
                       tbl->row_count());
      } else {
        MergedCallsite saved_callsite = it->first;
        callsites_to_rowid.erase(saved_callsite);
        if (saved_callsite.source_file != merged_callsite.source_file) {
          saved_callsite.source_file = std::nullopt;
        }
        if (saved_callsite.line_number != merged_callsite.line_number) {
          saved_callsite.line_number = std::nullopt;
        }
        callsites_to_rowid[saved_callsite] = it->second;
      }
      parent_idx = it->second;
    }

    for (const auto& it : callsites_to_rowid) {
      auto ref = (*tbl)[it.second];
      if (it.first.source_file) {
        ref.set_source_file(*it.first.source_file);
      }
      if (it.first.line_number) {
        ref.set_line_number(*it.first.line_number);
      }
    }

    PERFETTO_CHECK(parent_idx);
    callsite_to_merged_callsite[i] = *parent_idx;
  }

  return {std::move(tbl), callsite_to_merged_callsite};
}

static std::unique_ptr<tables::ExperimentalFlamegraphTable>
BuildFlamegraphTableHeapSizeAndCount(
    tables::HeapProfileAllocationTable::ConstCursor& it,
    std::unique_ptr<tables::ExperimentalFlamegraphTable> tbl,
    const std::vector<uint32_t>& callsite_to_merged_callsite) {
  for (; !it.Eof(); it.Next()) {
    int64_t size = it.size();
    int64_t count = it.count();
    tables::StackProfileCallsiteTable::Id callsite_id = it.callsite_id();

    PERFETTO_CHECK((size <= 0 && count <= 0) || (size >= 0 && count >= 0));
    uint32_t merged_idx = callsite_to_merged_callsite[callsite_id.value];
    tables::ExperimentalFlamegraphTable::RowReference ref = (*tbl)[merged_idx];

    // On old heapprofd producers, the count field is incorrectly set and we
    // zero it in proto_trace_parser.cc.
    // As such, we cannot depend on count == 0 to imply size == 0, so we check
    // for both of them separately.
    if (size > 0) {
      ref.set_alloc_size(ref.alloc_size() + size);
    }
    if (count > 0) {
      ref.set_alloc_count(ref.alloc_count() + count);
    }
    ref.set_size(ref.size() + size);
    ref.set_count(ref.count() + count);
  }

  // BACKWARD PASS:
  // Propagate sizes to parents.
  for (int64_t i = tbl->row_count() - 1; i >= 0; --i) {
    auto idx = static_cast<uint32_t>(i);
    auto ref = (*tbl)[idx];

    ref.set_cumulative_size(ref.cumulative_size() + ref.size());
    ref.set_cumulative_count(ref.cumulative_count() + ref.count());
    ref.set_cumulative_alloc_size(ref.cumulative_alloc_size() +
                                  ref.alloc_size());
    ref.set_cumulative_alloc_count(ref.cumulative_alloc_count() +
                                   ref.alloc_count());

    auto parent = ref.parent_id();
    if (parent) {
      auto parent_row = *tbl->FindById(*parent);
      parent_row.set_cumulative_size(parent_row.cumulative_size() +
                                     ref.cumulative_size());
      parent_row.set_cumulative_count(parent_row.cumulative_count() +
                                      ref.cumulative_count());
      parent_row.set_cumulative_alloc_size(parent_row.cumulative_alloc_size() +
                                           ref.cumulative_alloc_size());
      parent_row.set_cumulative_alloc_count(
          parent_row.cumulative_alloc_count() + ref.cumulative_alloc_count());
    }
  }
  return tbl;
}

static std::unique_ptr<tables::ExperimentalFlamegraphTable>
BuildFlamegraphTableCallstackSizeAndCount(
    tables::PerfSampleTable::ConstCursor& cursor,
    std::unique_ptr<tables::ExperimentalFlamegraphTable> tbl,
    const std::vector<uint32_t>& callsite_to_merged_callsite,
    const std::unordered_set<uint32_t>& utids) {
  for (; !cursor.Eof(); cursor.Next()) {
    if (utids.find(cursor.utid()) == utids.end()) {
      continue;
    }

    uint32_t callsite_id = cursor.callsite_id().value_or(CallsiteId(0u)).value;
    int64_t ts = cursor.ts();
    uint32_t merged_idx = callsite_to_merged_callsite[callsite_id];
    auto merged_row_ref = (*tbl)[merged_idx];
    merged_row_ref.set_size(merged_row_ref.size() + 1);
    merged_row_ref.set_count(merged_row_ref.count() + 1);
    merged_row_ref.set_ts(ts);
  }

  // BACKWARD PASS:
  // Propagate sizes to parents.
  for (int64_t i = tbl->row_count() - 1; i >= 0; --i) {
    auto idx = static_cast<uint32_t>(i);

    auto row = (*tbl)[idx];
    row.set_cumulative_size(row.cumulative_size() + row.size());
    row.set_cumulative_count(row.cumulative_count() + row.count());

    auto parent = (*tbl)[idx].parent_id();
    if (parent) {
      auto parent_row = *tbl->FindById(*parent);
      parent_row.set_cumulative_size(parent_row.cumulative_size() +
                                     row.cumulative_size());
      parent_row.set_cumulative_count(parent_row.cumulative_count() +
                                      row.cumulative_count());
    }
  }
  return tbl;
}

std::unique_ptr<tables::ExperimentalFlamegraphTable> BuildHeapProfileFlamegraph(
    TraceStorage* storage,
    UniquePid upid,
    int64_t timestamp) {
  const tables::HeapProfileAllocationTable& allocation_tbl =
      storage->heap_profile_allocation_table();
  // PASS OVER ALLOCATIONS:
  // Aggregate allocations into the newly built tree.
  auto cursor = allocation_tbl.CreateCursor({
      dataframe::FilterSpec{
          tables::HeapProfileAllocationTable::ColumnIndex::ts,
          0,
          dataframe::Le{},
          {},
      },
      dataframe::FilterSpec{
          tables::HeapProfileAllocationTable::ColumnIndex::upid,
          1,
          dataframe::Eq{},
          {},
      },
  });
  cursor.SetFilterValueUnchecked(0, timestamp);
  cursor.SetFilterValueUnchecked(1, upid);
  cursor.Execute();
  if (cursor.Eof()) {
    return nullptr;
  }
  StringId profile_type = storage->InternString("native");
  FlamegraphTableAndMergedCallsites table_and_callsites =
      BuildFlamegraphTableTreeStructure(storage, upid, std::nullopt, timestamp,
                                        profile_type);
  return BuildFlamegraphTableHeapSizeAndCount(
      cursor, std::move(table_and_callsites.tbl),
      table_and_callsites.callsite_to_merged_callsite);
}

std::unique_ptr<tables::ExperimentalFlamegraphTable>
BuildNativeCallStackSamplingFlamegraph(
    TraceStorage* storage,
    std::optional<UniquePid> upid,
    std::optional<std::string> upid_group,
    const std::vector<TimeConstraints>& time_constraints) {
  // 1. Extract required upids from input.
  std::unordered_set<UniquePid> upids;
  if (upid) {
    upids.insert(*upid);
  } else {
    for (base::StringSplitter sp(*upid_group, ','); sp.Next();) {
      std::optional<uint32_t> maybe = base::CStringToUInt32(sp.cur_token());
      if (maybe) {
        upids.insert(*maybe);
      }
    }
  }

  // 2. Create set of all utids mapped to the given vector of upids
  std::unordered_set<UniqueTid> utids;
  {
    std::vector<dataframe::FilterSpec> thread_fs = {
        dataframe::FilterSpec{
            tables::ThreadTable::ColumnIndex::upid,
            0,
            dataframe::IsNotNull{},
            {},
        },
    };
    auto cursor = storage->thread_table().CreateCursor(thread_fs);
    for (cursor.Execute(); !cursor.Eof(); cursor.Next()) {
      if (upids.count(*cursor.upid())) {
        utids.emplace(cursor.id());
      }
    }
  }

  // 3. Get all row indices in perf_sample that have callstacks (some samples
  // can have only counter values), are in timestamp bounds and correspond to
  // the requested utids.
  std::vector<dataframe::FilterSpec> cs;
  for (uint32_t i = 0; i < time_constraints.size(); ++i) {
    const auto& tc = time_constraints[i];
    if (!tc.op.Is<dataframe::Gt>() && !tc.op.Is<dataframe::Lt>() &&
        !tc.op.Is<dataframe::Ge>() && !tc.op.Is<dataframe::Le>()) {
      PERFETTO_FATAL("Filter operation %u not permitted for perf.",
                     tc.op.index());
    }
    cs.emplace_back(dataframe::FilterSpec{
        tables::PerfSampleTable::ColumnIndex::ts,
        i,
        tc.op,
        {},
    });
  }
  cs.push_back(dataframe::FilterSpec{
      tables::PerfSampleTable::ColumnIndex::callsite_id,
      static_cast<uint32_t>(time_constraints.size()),
      dataframe::IsNotNull{},
      {},
  });
  auto cursor = storage->perf_sample_table().CreateCursor(std::move(cs));
  for (uint32_t i = 0; i < time_constraints.size(); ++i) {
    cursor.SetFilterValueUnchecked(i, time_constraints[i].value);
  }
  cursor.Execute();

  // The logic underneath is selecting a default timestamp to be used by all
  // frames which do not have a timestamp. The timestamp is taken from the
  // query value and it's not meaningful for the row. It prevents however the
  // rows with no timestamp from being filtered out by Sqlite, after we create
  // the table ExperimentalFlamegraphTable in this class.
  int64_t default_timestamp = 0;
  if (!time_constraints.empty()) {
    const auto& tc = time_constraints[0];
    if (tc.op.Is<dataframe::Gt>()) {
      default_timestamp = tc.value + 1;
    } else if (tc.op.Is<dataframe::Lt>()) {
      default_timestamp = tc.value - 1;
    } else {
      default_timestamp = tc.value;
    }
  }

  // 4. Build the flamegraph structure.
  FlamegraphTableAndMergedCallsites table_and_callsites =
      BuildFlamegraphTableTreeStructure(storage, upid, upid_group,
                                        default_timestamp,
                                        storage->InternString("perf"));
  return BuildFlamegraphTableCallstackSizeAndCount(
      cursor, std::move(table_and_callsites.tbl),
      table_and_callsites.callsite_to_merged_callsite, utids);
}

}  // namespace perfetto::trace_processor
