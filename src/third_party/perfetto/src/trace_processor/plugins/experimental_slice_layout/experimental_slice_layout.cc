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

#include "src/trace_processor/plugins/experimental_slice_layout/experimental_slice_layout.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/dataframe/typed_cursor.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/core/plugin/registration.h"
#include "src/trace_processor/plugins/experimental_slice_layout/experimental_slice_layout_impl.h"
#include "src/trace_processor/plugins/experimental_slice_layout/tables_py.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

namespace {

struct GroupInfo {
  GroupInfo(int64_t _start, int64_t _end, uint32_t _max_depth)
      : start(_start), end(_end), max_depth(_max_depth) {}
  int64_t start;
  int64_t end;
  uint32_t layout_depth = 0;
  uint32_t max_depth;
};

// A single layout row: the [start, end) time ranges occupied on it, sorted by
// start and disjoint. Being disjoint their ends are increasing too, so the last
// range's end is the row's high-water mark.
using Row = std::vector<std::pair<int64_t, int64_t>>;

// First range that starts at or after |ts|.
Row::const_iterator LowerBound(const Row& row, int64_t ts) {
  return std::lower_bound(row.begin(), row.end(), ts,
                          [](const std::pair<int64_t, int64_t>& r, int64_t t) {
                            return r.first < t;
                          });
}

// True if [start, end) overlaps nothing occupied on |row|.
bool RowIsFree(const Row& row, int64_t start, int64_t end) {
  if (row.empty() || start >= row.back().second) {
    return true;  // empty, or clear from the last occupied range onwards
  }
  auto it = LowerBound(row, start);
  if (it != row.end() && it->first < end) {
    return false;
  }
  if (it != row.begin() && std::prev(it)->second > start) {
    return false;
  }
  return true;
}

// Record [start, end) as occupied. Placements on a row almost always arrive in
// time order, so this is an append; the rare out-of-order case shifts.
void RowMark(Row& row, int64_t start, int64_t end) {
  if (row.empty() || start >= row.back().first) {
    row.emplace_back(start, end);
  } else {
    row.insert(LowerBound(row, start), {start, end});
  }
}

}  // namespace

ExperimentalSliceLayout::Cursor::Cursor(
    StringPool* string_pool,
    const tables::SliceTable* table,
    std::unordered_map<StringPool::Id, std::vector<CachedRow>>* cache)
    : string_pool_(string_pool),
      slice_table_(table),
      table_(string_pool),
      cache_(cache) {}

bool ExperimentalSliceLayout::Cursor::Run(
    const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 1);
  table_.Clear();

  if (arguments[0].type != SqlValue::Type::kString) {
    return OnFailure(base::ErrStatus("invalid input track id list"));
  }

  const char* filter_string = arguments[0].string_value;
  using FilterValue = dataframe::TypedCursor::FilterValue;
  std::vector<FilterValue> selected_track_ids;
  for (base::StringSplitter sp(filter_string, ','); sp.Next();) {
    std::optional<uint32_t> maybe = base::CStringToUInt32(sp.cur_token());
    if (maybe) {
      selected_track_ids.emplace_back(static_cast<int64_t>(maybe.value()));
    }
  }

  // Try and find the table in the cache.
  StringPool::Id filter_id = string_pool_->InternString(filter_string);
  auto cache_it = cache_->find(filter_id);
  if (cache_it != cache_->end()) {
    for (const auto& row : cache_it->second) {
      table_.Insert({row.id, row.layout_depth});
    }
    return OnSuccess(&table_.dataframe());
  }

  // Find all the slices for the tracks we want to filter using an In filter
  // on track_id to avoid a full table scan.
  auto track_cursor = slice_table_->CreateCursor(
      {dataframe::FilterSpec{tables::SliceTable::ColumnIndex::track_id, 0,
                             dataframe::In{}, std::nullopt}});
  track_cursor.SetFilterValueListUnchecked(
      0, selected_track_ids.data(),
      static_cast<uint32_t>(selected_track_ids.size()));

  std::vector<tables::SliceTable::RowNumber> rows;
  for (track_cursor.Execute(); !track_cursor.Eof(); track_cursor.Next()) {
    rows.emplace_back(track_cursor.ToRowNumber());
  }

  // Compute the table and add it to the cache for future use.
  auto res = ComputeLayoutTable(rows);
  for (const auto& row : res) {
    table_.Insert({row.id, row.layout_depth});
  }
  cache_->emplace(filter_id, std::move(res));
  return OnSuccess(&table_.dataframe());
}

// The problem we're trying to solve is this: given a number of tracks each of
// which contain a number of 'stalactites' - depth 0 slices and all their
// children - layout the stalactites to minimize vertical depth without
// changing the horizontal (time) position. So given two tracks:
// Track A:
//     aaaaaaaaa       aaa
//                      aa
//                       a
// Track B:
//      bbb       bbb    bbb
//       b         b      b
// The result could be something like:
//     aaaaaaaaa  bbb  aaa
//                 b    aa
//      bbb              a
//       b
//                       bbb
//                        b
// We do this by computing an additional column: layout_depth, the vertical
// position of each slice.
//
// Each stalactite is reduced to a bounding box (start, end, max depth) and the
// boxes are packed into rows. Two choices keep the layout compact:
//  1. Boxes are placed tallest first, ties broken by start ts. Packing a wide
//     shallow box before a tall box it overlaps would wedge the tall box (and
//     its whole subtree) downwards; placing tall boxes first avoids this. When
//     every box has the same height this reduces to start ts order, matching
//     the historical behaviour so single-depth tracks are unchanged.
//  2. Each box takes the lowest run of (max depth + 1) rows that are free over
//     its whole time range. Slices on one track never overlap, so at any point
//     in time at most one box per track is live and at most 'number of tracks'
//     rows are occupied. That bounds how far this scan ever walks, so we just
//     track the occupied time ranges per row rather than reasoning about which
//     boxes overlap which.
std::vector<ExperimentalSliceLayout::CachedRow>
ExperimentalSliceLayout::Cursor::ComputeLayoutTable(
    const std::vector<tables::SliceTable::RowNumber>& rows) {
  if (rows.empty()) {
    return {};
  }

  // Step 1: reduce each group (a depth 0 root and its descendants) to a
  // bounding box. Slices arrive in id order so a parent is always seen before
  // its children, letting us map every slice to its group in a single pass.
  // |id_to_group| resolves a parent id; |slice_group| records each row's group
  // positionally so the emit pass never has to hash.
  base::FlatHashMap<uint32_t, uint32_t> id_to_group;
  std::vector<uint32_t> slice_group;
  slice_group.reserve(rows.size());
  std::vector<GroupInfo> groups;
  for (tables::SliceTable::RowNumber i : rows) {
    auto ref = i.ToRowReference(*slice_table_);
    uint32_t depth = ref.depth();
    int64_t start = ref.ts();
    int64_t dur = ref.dur();
    int64_t end = dur == -1 ? std::numeric_limits<int64_t>::max() : start + dur;

    uint32_t group_idx;
    std::optional<tables::SliceTable::Id> parent = ref.parent_id();
    if (parent) {
      group_idx = *id_to_group.Find(parent->value);
      GroupInfo& group = groups[group_idx];
      group.max_depth = std::max(group.max_depth, depth);
      group.end = std::max(group.end, end);
    } else {
      group_idx = static_cast<uint32_t>(groups.size());
      groups.emplace_back(start, end, depth);
    }
    id_to_group.Insert(ref.id().value, group_idx);
    slice_group.push_back(group_idx);
  }

  // Step 2: order the groups tallest first, ties broken by start ts then id.
  std::vector<uint32_t> order(groups.size());
  for (uint32_t i = 0; i < groups.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
    const GroupInfo& ga = groups[a];
    const GroupInfo& gb = groups[b];
    if (ga.max_depth != gb.max_depth) {
      return ga.max_depth > gb.max_depth;
    }
    if (ga.start != gb.start) {
      return ga.start < gb.start;
    }
    return a < b;
  });

  // Step 3: place each group at the lowest run of (max depth + 1) rows that is
  // free over its whole time range, then mark those rows occupied.
  std::vector<Row> occupied;
  for (uint32_t group_idx : order) {
    GroupInfo& group = groups[group_idx];

    uint32_t layout_depth = 0;
    uint32_t run = 0;
    for (uint32_t r = 0;; ++r) {
      if (r >= occupied.size() ||
          RowIsFree(occupied[r], group.start, group.end)) {
        if (run == 0) {
          layout_depth = r;
        }
        if (++run == group.max_depth + 1) {
          break;
        }
      } else {
        run = 0;
      }
    }
    group.layout_depth = layout_depth;

    uint32_t top = layout_depth + group.max_depth;
    if (occupied.size() <= top) {
      occupied.resize(top + 1);
    }
    for (uint32_t r = layout_depth; r <= top; ++r) {
      RowMark(occupied[r], group.start, group.end);
    }
  }

  // Step 4: emit each slice at its own depth plus its group's root depth.
  std::vector<CachedRow> cached;
  cached.reserve(rows.size());
  for (uint32_t i = 0; i < rows.size(); ++i) {
    auto ref = rows[i].ToRowReference(*slice_table_);
    uint32_t group_depth = groups[slice_group[i]].layout_depth;
    cached.emplace_back(ExperimentalSliceLayout::CachedRow{
        ref.id(), ref.depth() + group_depth});
  }
  return cached;
}

ExperimentalSliceLayout::ExperimentalSliceLayout(
    StringPool* string_pool,
    const tables::SliceTable* table)
    : string_pool_(string_pool), slice_table_(table) {}
ExperimentalSliceLayout::~ExperimentalSliceLayout() = default;

std::unique_ptr<StaticTableFunction::Cursor>
ExperimentalSliceLayout::MakeCursor() {
  return std::make_unique<Cursor>(string_pool_, slice_table_, &cache_);
}

dataframe::DataframeSpec ExperimentalSliceLayout::CreateSpec() {
  return tables::ExperimentalSliceLayoutTable::kSpec.ToUntypedDataframeSpec();
}

std::string ExperimentalSliceLayout::TableName() {
  return "experimental_slice_layout";
}

uint32_t ExperimentalSliceLayout::GetArgumentCount() const {
  return 1;
}

namespace experimental_slice_layout {
namespace {

class ExperimentalSliceLayoutPlugin
    : public Plugin<ExperimentalSliceLayoutPlugin> {
 public:
  ~ExperimentalSliceLayoutPlugin() override;

  void RegisterStaticTableFunctions(
      PerfettoSqlConnection*,
      std::vector<std::unique_ptr<StaticTableFunction>>& fns) override {
    TraceStorage* storage = trace_context_->storage.get();
    fns.emplace_back(std::make_unique<ExperimentalSliceLayout>(
        storage->mutable_string_pool(), &storage->slice_table()));
  }
};

ExperimentalSliceLayoutPlugin::~ExperimentalSliceLayoutPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<ExperimentalSliceLayoutPlugin>();
      },
      ExperimentalSliceLayoutPlugin::kPluginId,
      ExperimentalSliceLayoutPlugin::kDepIds.data(),
      ExperimentalSliceLayoutPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace experimental_slice_layout
}  // namespace perfetto::trace_processor
