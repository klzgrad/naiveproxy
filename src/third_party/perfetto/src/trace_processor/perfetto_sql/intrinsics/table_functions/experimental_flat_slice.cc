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

#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/experimental_flat_slice.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/tables/track_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

ExperimentalFlatSlice::Cursor::Cursor(TraceProcessorContext* context)
    : context_(context), table_(context_->storage->mutable_string_pool()) {}

bool ExperimentalFlatSlice::Cursor::Run(
    const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 2);
  table_.Clear();

  if (arguments[0].is_null() || arguments[1].is_null()) {
    // ComputeFlatSliceTable might not handle nulls gracefully.
    // Return an empty table.
    // No OnFailure needed as this is valid input leading to an empty result.
    return OnSuccess(&table_.dataframe());
  }

  if (arguments[0].type != SqlValue::kLong) {
    return OnFailure(base::ErrStatus("start timestamp must be an integer"));
  }
  if (arguments[1].type != SqlValue::kLong) {
    return OnFailure(base::ErrStatus("end timestamp must be an integer"));
  }

  std::unique_ptr<tables::ExperimentalFlatSliceTable> constructed_table =
      ExperimentalFlatSlice::ComputeFlatSliceTable(
          context_->storage->slice_table(),
          context_->storage->mutable_string_pool(), arguments[0].AsLong(),
          arguments[1].AsLong());
  if (!constructed_table) {
    return OnFailure(
        base::ErrStatus("Failed to compute ExperimentalFlatSliceTable"));
  }
  table_ = std::move(*constructed_table);
  return OnSuccess(&table_.dataframe());
}

ExperimentalFlatSlice::ExperimentalFlatSlice(TraceProcessorContext* context)
    : context_(context) {}

std::unique_ptr<tables::ExperimentalFlatSliceTable>
ExperimentalFlatSlice::ComputeFlatSliceTable(const tables::SliceTable& slice,
                                             StringPool* pool,
                                             int64_t start_bound,
                                             int64_t end_bound) {
  std::unique_ptr<tables::ExperimentalFlatSliceTable> out(
      new tables::ExperimentalFlatSliceTable(pool));

  auto insert_slice = [&](uint32_t i, int64_t ts,
                          tables::TrackTable::Id track_id) {
    auto rr = slice[i];
    tables::ExperimentalFlatSliceTable::Row row;
    row.ts = ts;
    row.dur = -1;
    row.track_id = track_id;
    row.category = rr.category();
    row.name = rr.name();
    row.arg_set_id = rr.arg_set_id();
    row.source_id = rr.id();
    row.start_bound = start_bound;
    row.end_bound = end_bound;
    return out->Insert(row).row;
  };
  auto insert_sentinel = [&](int64_t ts, TrackId track_id) {
    tables::ExperimentalFlatSliceTable::Row row;
    row.ts = ts;
    row.dur = -1;
    row.track_id = track_id;
    row.category = kNullStringId;
    row.name = kNullStringId;
    row.arg_set_id = std::nullopt;
    row.source_id = std::nullopt;
    row.start_bound = start_bound;
    row.end_bound = end_bound;
    return out->Insert(row).row;
  };

  auto terminate_slice = [&](uint32_t out_row, int64_t end_ts) {
    auto rr = (*out)[out_row];
    PERFETTO_DCHECK(rr.dur() == -1);
    rr.set_dur(end_ts - rr.ts());
  };

  struct ActiveSlice {
    std::optional<uint32_t> source_row;
    uint32_t out_row = std::numeric_limits<uint32_t>::max();

    bool is_sentinel() const { return !source_row; }
  };
  struct Track {
    std::vector<uint32_t> parents;
    ActiveSlice active;
    bool initialized = false;
  };
  std::unordered_map<TrackId, Track> tracks;

  auto maybe_terminate_active_slice = [&](const Track& t, int64_t fin_ts) {
    auto rr = slice[t.active.source_row.value()];
    int64_t ts = rr.ts();
    int64_t dur = rr.dur();
    if (dur == -1 || ts + dur > fin_ts)
      return false;

    terminate_slice(t.active.out_row, ts + dur);
    return true;
  };

  // Post-condition: |tracks[track_id].active| will always point to
  // a slice which finishes after |fin_ts| and has a |dur| == -1 in
  // |out|.
  auto output_slices_before = [&](TrackId track_id, int64_t fin_ts) {
    auto& t = tracks[track_id];

    // A sentinel slice cannot have parents.
    PERFETTO_DCHECK(!t.active.is_sentinel() || t.parents.empty());

    // If we have a sentinel slice active, we have nothing to output.
    if (t.active.is_sentinel())
      return;

    // Try and terminate the current slice (if it ends before |fin_ts|)
    // If we cannot terminate it, then we leave it as pending for the caller
    // to terminate.
    if (!maybe_terminate_active_slice(t, fin_ts))
      return;

    // Next, add any parents as appropriate.
    for (int64_t i = static_cast<int64_t>(t.parents.size()) - 1; i >= 0; --i) {
      uint32_t source_row = t.parents[static_cast<size_t>(i)];
      t.parents.pop_back();

      auto rr = (*out)[t.active.out_row];
      int64_t active_ts = rr.ts();
      int64_t active_dur = rr.dur();
      PERFETTO_DCHECK(active_dur != -1);

      t.active.source_row = source_row;
      t.active.out_row =
          insert_slice(source_row, active_ts + active_dur, track_id);

      if (!maybe_terminate_active_slice(t, fin_ts))
        break;
    }

    if (!t.parents.empty())
      return;

    // If the active slice is a sentinel, the check at the top of this function
    // should have caught it; all code only adds slices from source.
    PERFETTO_DCHECK(!t.active.is_sentinel());

    auto rr = (*out)[t.active.out_row];
    int64_t ts = rr.ts();
    int64_t dur = rr.dur();

    // If the active slice is unfinshed, we return that for the caller to
    // terminate.
    if (dur == -1)
      return;

    // Otherwise, Add a sentinel slice after the end of the active slice.
    t.active.source_row = std::nullopt;
    t.active.out_row = insert_sentinel(ts + dur, track_id);
  };

  for (auto it = slice.IterateRows(); it; ++it) {
    // TODO(lalitm): this can be optimized using a O(logn) lower bound/filter.
    // Not adding for now as a premature optimization but may be needed down the
    // line.
    int64_t ts = it.ts();
    if (ts < start_bound)
      continue;

    if (ts >= end_bound)
      break;

    // Ignore instants as they don't factor into flat slice at all.
    if (it.dur() == 0)
      continue;

    TrackId track_id = it.track_id();
    Track& track = tracks[track_id];

    // Initialize the track (if needed) by adding a sentinel slice starting at
    // start_bound.
    bool is_root = it.depth() == 0;
    if (!track.initialized) {
      // If we are uninitialized and our start box picks up slices mid way
      // through startup, wait until we reach a root slice.
      if (!is_root)
        continue;

      track.active.out_row = insert_sentinel(start_bound, track_id);
      track.initialized = true;
    }
    output_slices_before(track_id, ts);
    terminate_slice(track.active.out_row, ts);

    // We should have sentinel slices iff the slice is a root.
    PERFETTO_DCHECK(track.active.is_sentinel() == is_root);

    // If our current slice has a parent, that must be the current active slice.
    if (!is_root) {
      track.parents.push_back(*track.active.source_row);
    }

    // The depth of our slice should also match the depth of the parent stack
    // (after adding the previous slice).
    PERFETTO_DCHECK(track.parents.size() == it.depth());

    track.active.source_row = it.row_number().row_number();
    track.active.out_row =
        insert_slice(it.row_number().row_number(), ts, track_id);
  }

  for (const auto& track : tracks) {
    // If the track is not initialized, don't add anything.
    if (!track.second.initialized)
      continue;

    // First, terminate any hanging slices.
    output_slices_before(track.first, end_bound);

    // Second, force terminate the final slice to the end bound.
    terminate_slice(track.second.active.out_row, end_bound);
  }

  return out;
}

std::unique_ptr<StaticTableFunction::Cursor>
ExperimentalFlatSlice::MakeCursor() {
  return std::make_unique<Cursor>(context_);
}

dataframe::DataframeSpec ExperimentalFlatSlice::CreateSpec() {
  return tables::ExperimentalFlatSliceTable::kSpec.ToUntypedDataframeSpec();
}

std::string ExperimentalFlatSlice::TableName() {
  return "experimental_flat_slice";
}

uint32_t ExperimentalFlatSlice::GetArgumentCount() const {
  return 2;
}

}  // namespace perfetto::trace_processor
