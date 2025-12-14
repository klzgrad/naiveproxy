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

#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/experimental_flamegraph.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/proto/heap_graph_tracker.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/flamegraph_construction_algorithms.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

namespace {

base::StatusOr<ExperimentalFlamegraph::ProfileType> ExtractProfileType(
    const std::string& profile_name) {
  if (profile_name == "graph") {
    return ExperimentalFlamegraph::ProfileType::kGraph;
  }
  if (profile_name == "native") {
    return ExperimentalFlamegraph::ProfileType::kHeapProfile;
  }
  if (profile_name == "perf") {
    return ExperimentalFlamegraph::ProfileType::kPerf;
  }
  return base::ErrStatus(
      "experimental_flamegraph: Could not recognize profile type: %s.",
      profile_name.c_str());
}

base::StatusOr<int64_t> ParseTimeConstraintTs(const std::string& c,
                                              uint32_t offset) {
  std::optional<int64_t> ts = base::CStringToInt64(&c[offset]);
  if (!ts) {
    return base::ErrStatus(
        "experimental_flamegraph: Unable to parse timestamp");
  }
  return *ts;
}

base::StatusOr<TimeConstraints> ParseTimeConstraint(const std::string& c) {
  if (base::StartsWith(c, "=")) {
    ASSIGN_OR_RETURN(int64_t ts, ParseTimeConstraintTs(c, 1));
    return TimeConstraints{dataframe::Eq{}, ts};
  }
  if (base::StartsWith(c, ">=")) {
    ASSIGN_OR_RETURN(int64_t ts, ParseTimeConstraintTs(c, 2));
    return TimeConstraints{dataframe::Ge{}, ts};
  }
  if (base::StartsWith(c, ">")) {
    ASSIGN_OR_RETURN(int64_t ts, ParseTimeConstraintTs(c, 1));
    return TimeConstraints{dataframe::Gt{}, ts};
  }
  if (base::StartsWith(c, "<=")) {
    ASSIGN_OR_RETURN(int64_t ts, ParseTimeConstraintTs(c, 2));
    return TimeConstraints{dataframe::Le{}, ts};
  }
  if (base::StartsWith(c, ">=")) {
    ASSIGN_OR_RETURN(int64_t ts, ParseTimeConstraintTs(c, 2));
    return TimeConstraints{dataframe::Lt{}, ts};
  }
  return base::ErrStatus("experimental_flamegraph: Unknown time constraint");
}

base::StatusOr<std::vector<TimeConstraints>> ExtractTimeConstraints(
    const SqlValue& value) {
  PERFETTO_DCHECK(value.is_null() || value.type == SqlValue::kString);
  std::vector<TimeConstraints> constraints;
  if (value.is_null()) {
    return constraints;
  }
  std::vector<std::string> raw_cs = base::SplitString(value.AsString(), ",");
  for (const std::string& c : raw_cs) {
    ASSIGN_OR_RETURN(TimeConstraints tc, ParseTimeConstraint(c));
    constraints.push_back(tc);
  }
  return constraints;
}

// For filtering, this method uses the same constraints as
// ExperimentalFlamegraph::ValidateConstraints and should therefore
// be kept in sync.
base::StatusOr<ExperimentalFlamegraph::InputValues> GetFlamegraphInputValues(
    const std::vector<SqlValue>& arguments) {
  PERFETTO_CHECK(arguments.size() == 6);

  const SqlValue& raw_profile_type = arguments[0];
  if (raw_profile_type.type != SqlValue::kString) {
    return base::ErrStatus(
        "experimental_flamegraph: profile_type must be an string");
  }
  const SqlValue& ts = arguments[1];
  if (ts.type != SqlValue::kLong && !ts.is_null()) {
    return base::ErrStatus("experimental_flamegraph: ts must be an integer");
  }
  const SqlValue& ts_constraints = arguments[2];
  if (ts_constraints.type != SqlValue::kString && !ts_constraints.is_null()) {
    return base::ErrStatus(
        "experimental_flamegraph: ts constraint must be an string");
  }
  const SqlValue& upid = arguments[3];
  if (upid.type != SqlValue::kLong && !upid.is_null()) {
    return base::ErrStatus("experimental_flamegraph: upid must be an integer");
  }
  const SqlValue& upid_group = arguments[4];
  if (upid_group.type != SqlValue::kString && !upid_group.is_null()) {
    return base::ErrStatus(
        "experimental_flamegraph: upid_group must be an string");
  }
  const SqlValue& focus_str = arguments[5];
  if (focus_str.type != SqlValue::kString && !focus_str.is_null()) {
    return base::ErrStatus(
        "experimental_flamegraph: focus_str must be an string");
  }

  if (ts.is_null() && ts_constraints.is_null()) {
    return base::ErrStatus(
        "experimental_flamegraph: one of ts and ts_constraints must not be "
        "null");
  }
  if (upid.is_null() && upid_group.is_null()) {
    return base::ErrStatus(
        "experimental_flamegraph: one of upid or upid_group must not be null");
  }
  ASSIGN_OR_RETURN(std::vector<TimeConstraints> time_constraints,
                   ExtractTimeConstraints(ts_constraints));
  ASSIGN_OR_RETURN(ExperimentalFlamegraph::ProfileType profile_type,
                   ExtractProfileType(raw_profile_type.AsString()));
  return ExperimentalFlamegraph::InputValues{
      profile_type,
      ts.is_null() ? std::nullopt : std::make_optional(ts.AsLong()),
      std::move(time_constraints),
      upid.is_null() ? std::nullopt
                     : std::make_optional(static_cast<uint32_t>(upid.AsLong())),
      upid_group.is_null() ? std::nullopt
                           : std::make_optional(upid_group.AsString()),
      focus_str.is_null() ? std::nullopt
                          : std::make_optional(focus_str.AsString()),
  };
}

class Matcher {
 public:
  explicit Matcher(const std::string& str) : focus_str_(base::ToLower(str)) {}
  Matcher(const Matcher&) = delete;
  Matcher& operator=(const Matcher&) = delete;

  bool matches(const std::string& s) const {
    // TODO(149833691): change to regex.
    // We cannot use regex.h (does not exist in windows) or std regex (throws
    // exceptions).
    return base::Contains(base::ToLower(s), focus_str_);
  }

 private:
  const std::string focus_str_;
};

enum class FocusedState : uint8_t {
  kNotFocused,
  kFocusedPropagating,
  kFocusedNotPropagating,
};

using tables::ExperimentalFlamegraphTable;
std::vector<FocusedState> ComputeFocusedState(
    const StringPool& pool,
    const ExperimentalFlamegraphTable& table,
    const Matcher& focus_matcher) {
  // Each row corresponds to a node in the flame chart tree with its parent
  // ptr. Root trees (no parents) will have a null parent ptr.
  std::vector<FocusedState> focused(table.row_count());

  for (auto it = table.IterateRows(); it; ++it) {
    auto parent_id = it.parent_id();
    // Constraint: all descendants MUST come after their parents.
    PERFETTO_DCHECK(!parent_id.has_value() || *parent_id < it.id());

    auto i = it.row_number().row_number();
    if (focus_matcher.matches(pool.Get(it.name()).ToStdString())) {
      // Mark as focused
      focused[i] = FocusedState::kFocusedPropagating;
      auto current = parent_id;
      // Mark all parent nodes as focused
      while (current.has_value()) {
        auto c = *table.FindById(*current);
        uint32_t current_idx = c.ToRowNumber().row_number();
        if (focused[current_idx] != FocusedState::kNotFocused) {
          // We have already visited these nodes, skip
          break;
        }
        focused[current_idx] = FocusedState::kFocusedNotPropagating;
        current = c.parent_id();
      }
    } else if (parent_id.has_value() && focused[table.FindById(*parent_id)
                                                    ->ToRowNumber()
                                                    .row_number()] ==
                                            FocusedState::kFocusedPropagating) {
      // Focus cascades downwards.
      focused[i] = FocusedState::kFocusedPropagating;
    } else {
      focused[i] = FocusedState::kNotFocused;
    }
  }
  return focused;
}

struct CumulativeCounts {
  int64_t size;
  int64_t count;
  int64_t alloc_size;
  int64_t alloc_count;
};
std::unique_ptr<tables::ExperimentalFlamegraphTable> FocusTable(
    TraceStorage* storage,
    std::unique_ptr<ExperimentalFlamegraphTable> in,
    const std::string& focus_str) {
  if (in->row_count() == 0 || focus_str.empty()) {
    return in;
  }
  std::vector<FocusedState> focused_state =
      ComputeFocusedState(storage->string_pool(), *in, Matcher(focus_str));
  std::unique_ptr<ExperimentalFlamegraphTable> tbl(
      new tables::ExperimentalFlamegraphTable(storage->mutable_string_pool()));

  // Recompute cumulative counts
  std::vector<CumulativeCounts> node_to_cumulatives(in->row_count());
  for (int64_t idx = in->row_count() - 1; idx >= 0; --idx) {
    auto i = static_cast<uint32_t>(idx);
    auto rr = (*in)[i];
    if (focused_state[i] == FocusedState::kNotFocused) {
      continue;
    }
    auto& cumulatives = node_to_cumulatives[i];
    cumulatives.size += rr.size();
    cumulatives.count += rr.count();
    cumulatives.alloc_size += rr.alloc_size();
    cumulatives.alloc_count += rr.alloc_count();

    auto parent_id = rr.parent_id();
    if (parent_id.has_value()) {
      uint32_t parent_row =
          in->FindById(*parent_id)->ToRowNumber().row_number();
      auto& parent_cumulatives = node_to_cumulatives[parent_row];
      parent_cumulatives.size += cumulatives.size;
      parent_cumulatives.count += cumulatives.count;
      parent_cumulatives.alloc_size += cumulatives.alloc_size;
      parent_cumulatives.alloc_count += cumulatives.alloc_count;
    }
  }

  // Mapping between the old rows ('node') to the new identifiers.
  std::vector<ExperimentalFlamegraphTable::Id> node_to_id(in->row_count());
  for (auto it = in->IterateRows(); it; ++it) {
    uint32_t i = it.row_number().row_number();
    if (focused_state[i] == FocusedState::kNotFocused) {
      continue;
    }

    tables::ExperimentalFlamegraphTable::Row alloc_row{};
    // We must reparent the rows as every insertion will get its own
    // identifier.
    auto original_parent_id = it.parent_id();
    if (original_parent_id.has_value()) {
      auto original_idx =
          in->FindById(*original_parent_id)->ToRowNumber().row_number();
      alloc_row.parent_id = node_to_id[original_idx];
    }

    alloc_row.ts = it.ts();
    alloc_row.upid = it.upid();
    alloc_row.profile_type = it.profile_type();
    alloc_row.depth = it.depth();
    alloc_row.name = it.name();
    alloc_row.map_name = it.map_name();
    alloc_row.count = it.count();
    alloc_row.size = it.size();
    alloc_row.alloc_count = it.alloc_count();
    alloc_row.alloc_size = it.alloc_size();

    const auto& cumulative = node_to_cumulatives[i];
    alloc_row.cumulative_count = cumulative.count;
    alloc_row.cumulative_size = cumulative.size;
    alloc_row.cumulative_alloc_count = cumulative.alloc_count;
    alloc_row.cumulative_alloc_size = cumulative.alloc_size;
    node_to_id[i] = tbl->Insert(alloc_row).id;
  }
  return tbl;
}
}  // namespace

ExperimentalFlamegraph::Cursor::Cursor(TraceProcessorContext* context)
    : context_(context), table_(context_->storage->mutable_string_pool()) {}

bool ExperimentalFlamegraph::Cursor::Run(
    const std::vector<SqlValue>& arguments) {
  base::StatusOr<InputValues> values_status =
      GetFlamegraphInputValues(arguments);
  if (!values_status.ok()) {
    return OnFailure(values_status.status());
  }
  InputValues values = std::move(values_status.value());
  std::unique_ptr<tables::ExperimentalFlamegraphTable> constructed_table;
  switch (values.profile_type) {
    case ProfileType::kGraph: {
      if (!values.ts || !values.upid) {
        return OnFailure(base::ErrStatus(
            "experimental_flamegraph: ts and upid must be present for heap "
            "graph"));
      }
      constructed_table = HeapGraphTracker::Get(context_)->BuildFlamegraph(
          *values.ts, *values.upid);
      break;
    }
    case ProfileType::kHeapProfile: {
      if (!values.ts || !values.upid) {
        return OnFailure(base::ErrStatus(
            "experimental_flamegraph: ts and upid must be present for heap "
            "profile"));
      }
      constructed_table = BuildHeapProfileFlamegraph(context_->storage.get(),
                                                     *values.upid, *values.ts);
      break;
    }
    case ProfileType::kPerf: {
      constructed_table = BuildNativeCallStackSamplingFlamegraph(
          context_->storage.get(), values.upid, values.upid_group,
          values.time_constraints);
      break;
    }
  }
  if (!constructed_table) {
    return OnFailure(base::ErrStatus("Failed to build flamegraph"));
  }
  if (values.focus_str) {
    constructed_table =
        FocusTable(context_->storage.get(), std::move(constructed_table),
                   *values.focus_str);
  }
  table_ = std::move(*constructed_table);
  return OnSuccess(&table_.dataframe());
}

ExperimentalFlamegraph::ExperimentalFlamegraph(TraceProcessorContext* context)
    : context_(context) {}

ExperimentalFlamegraph::~ExperimentalFlamegraph() = default;

std::unique_ptr<StaticTableFunction::Cursor>
ExperimentalFlamegraph::MakeCursor() {
  return std::make_unique<Cursor>(context_);
}

dataframe::DataframeSpec ExperimentalFlamegraph::CreateSpec() {
  return tables::ExperimentalFlamegraphTable::kSpec.ToUntypedDataframeSpec();
}

std::string ExperimentalFlamegraph::TableName() {
  return tables::ExperimentalFlamegraphTable::Name();
}

uint32_t ExperimentalFlamegraph::GetArgumentCount() const {
  return 6;
}

}  // namespace perfetto::trace_processor
