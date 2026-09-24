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

#include "src/trace_processor/plugins/flamegraph/flamegraph.h"

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/endian.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "src/trace_processor/core/tree/tree_column_ops.h"
#include "src/trace_processor/core/tree/tree_path_interner.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/flex_vector.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/sort.h"

namespace perfetto::trace_processor::flamegraph {
namespace {

// A bit outside the SHOW_STACK mask. Once set, equality with the required
// SHOW_STACK mask is impossible, rejecting this path and all its descendants.
constexpr uint64_t kHideStackBit = uint64_t{1} << 63;

template <typename T>
bool TryAdd(T value, T* total) {
  static_assert(std::is_same_v<T, int64_t> || std::is_same_v<T, double>);
  if constexpr (std::is_same_v<T, int64_t>) {
    return base::CheckedAdd(*total, value, total);
  } else {
    *total += value;
    return true;
  }
}

template <typename T>
bool AccumulateSum(T value,
                   uint32_t destination,
                   core::Span<T> sums,
                   core::BitVector* has_value) {
  PERFETTO_DCHECK(destination < sums.size());
  PERFETTO_DCHECK(has_value->size() == sums.size());
  if (!has_value->is_set(destination)) {
    sums[destination] = value;
    has_value->set(destination);
    return true;
  }
  return TryAdd(value, &sums[destination]);
}

template <typename T>
core::Slab<T> AllocFilled(uint64_t size, T value) {
  core::Slab<T> slab = core::Slab<T>::Alloc(size);
  std::fill_n(slab.data(), size, value);
  return slab;
}

void UpdateNonZeroRows(const core::Tree::Column& column,
                       uint32_t offset,
                       core::Span<uint8_t> rows) {
  if (column.type.Is<core::Int64>()) {
    core::Span<const int64_t> values =
        column.unchecked_span<int64_t>().subspan(offset, rows.size());
    for (uint32_t row = 0; row < rows.size(); ++row) {
      rows[row] |= !base::IsZero(values[row]);
    }
    return;
  }
  PERFETTO_DCHECK(column.type.Is<core::Double>());
  core::Span<const double> values =
      column.unchecked_span<double>().subspan(offset, rows.size());
  for (uint32_t row = 0; row < rows.size(); ++row) {
    rows[row] |= !base::IsZero(values[row]);
  }
}

struct FilterMatches {
  uint64_t show_stack : Config::kMaxShowStackFilters;
  uint64_t view_pattern : 1;
  uint64_t hide_stack : 1;
  uint64_t hide_frame : 1;
};
static_assert(sizeof(FilterMatches) == 8);

constexpr FilterMatches kNoFilterMatches{};

// Everything later phases need from the root-to-row path traversal. Keeping
// the path-compressed retained frame avoids any later ancestor search.
struct PathState {
  // SHOW_STACK matches on retained frames, plus kHideStackBit if a retained
  // frame on this path matched HIDE_STACK. Removed frames inherit this value
  // unchanged, so their names cannot affect stack filtering.
  uint64_t stack_bits;

  // The nearest retained input row. A retained frame points to itself, while a
  // removed frame inherits its nearest retained ancestor.
  uint32_t retained_frame;
  uint8_t padding[4];

  bool IsRetained(uint32_t row) const;
  bool StackIsHidden() const;
  bool StackContributes(uint64_t required_show_stack_bits) const;
};
static_assert(sizeof(PathState) == 16);

static constexpr PathState kEmptyPathState =
    PathState{0, core::Tree::kNullParent, {}};

struct UpwardPath {
  uint32_t root;
  uint32_t leaf;
};
static_assert(sizeof(UpwardPath) == 8);

struct UpwardAnchor {
  uint32_t frame;
  UpwardPath path;
};
static_assert(sizeof(UpwardAnchor) == 12);

struct TreeConstituent {
  uint32_t frame;
  uint32_t node;
};
static_assert(sizeof(TreeConstituent) == 8);

// Self and cumulative values for one value column, indexed by combined node
// id: downward nodes first, then upward nodes offset by the downward size.
// Aggregate columns use the same combined id space.
struct MetricColumns {
  core::Tree::Column self;
  core::Tree::Column cumulative;
};

// Outputs of the three build stages, produced in order by Run().

// View-independent per-row analysis: frame identity, filter matching and
// path compression. Depends only on the input tree and the config's
// regexes; both halves are built from it.
struct PathAnalysis {
  std::vector<base::MurmurHashCombiner> frame_hashes;
  core::Slab<PathState> states;
  // Retained rows whose frame matches the view pattern. Allocated only when
  // the config has a view pattern.
  core::BitVector view_pattern_matches;
};

// The merged downward (descendant) half of the view.
struct DownwardHalf {
  explicit DownwardHalf(uint32_t interner_capacity) : tree(interner_capacity) {}

  core::TreePathInterner tree;
  // Input row -> merged node receiving its values, kNullParent when the row
  // is outside the downward half.
  core::Slab<uint32_t> node;
};

// The merged upward (ancestor) half of the view.
struct UpwardHalf {
  explicit UpwardHalf(uint32_t interner_capacity) : tree(interner_capacity) {}

  core::TreePathInterner tree;
  core::Slab<UpwardPath> bottom_up_paths;
  core::FlexVector<UpwardAnchor> pivot_anchors;
  core::FlexVector<TreeConstituent> aggregate_constituents;
};

// Compact two-level routing for aggregates. Output nodes reference retained
// frames, and retained frames reference the input rows folded into them.
struct AggregateRouting {
  core::Slab<uint32_t> output_frame_offsets;
  core::Slab<uint32_t> output_frames;
  core::Slab<uint32_t> frame_row_offsets;
  core::Slab<uint32_t> frame_rows;
  bool frame_rows_are_identity = false;
};

// Appends the textual form of an aggregated value: strings verbatim,
// numbers formatted directly so numeric columns need no per-row interning.
void AppendValue(StringPool::Id value,
                 const StringPool& pool,
                 std::string* out) {
  const NullTermStringView str = pool.Get(value);
  out->append(str.data(), str.size());
}
void AppendValue(int64_t value, const StringPool&, std::string* out) {
  base::StackString<32> str("%" PRId64, value);
  out->append(str.c_str(), str.len());
}
void AppendValue(double value, const StringPool&, std::string* out) {
  base::StackString<32> str("%.15g", value);
  out->append(str.c_str(), str.len());
  // Match SQLite's REAL rendering: integral values keep a trailing ".0".
  // The scan set also skips infinities and NaNs.
  if (strcspn(str.c_str(), ".eEnN") == str.len()) {
    out->append(".0");
  }
}

// Canonical 64-bit pattern used to count distinct aggregate values without
// comparing the values themselves (which -Wfloat-equal forbids for doubles).
uint64_t DistinctKey(StringPool::Id value) {
  return value.raw_id();
}
uint64_t DistinctKey(int64_t value) {
  return static_cast<uint64_t>(value);
}
uint64_t DistinctKey(double value) {
  if (base::IsZero(value)) {
    return 0;  // Canonicalize -0.0 to +0.0.
  }
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

struct NativeFrameRouting {
  core::Span<const PathState> states;
  core::Span<const uint32_t> downward_nodes;
  core::Span<const TreeConstituent> upward_constituents;
  uint32_t downward_size;
  uint64_t required_show_stack_bits;
  bool has_downward;
};

class AggregateOperator {
 public:
  enum class InputMode {
    kRowsToOutputs,
    kNativeRowsToFramesThenFramesToOutputs,
  };

  virtual ~AggregateOperator();
  virtual InputMode input_mode() const { return InputMode::kRowsToOutputs; }
  virtual base::Status UpdateBatch(core::Span<const uint32_t>,
                                   core::Span<const uint32_t>) {
    PERFETTO_FATAL("Aggregate operator does not support direct row updates");
  }
  virtual base::Status UpdateFrames(const NativeFrameRouting&) {
    PERFETTO_FATAL("Aggregate operator does not support frame aggregation");
  }
  virtual base::Status MergeFrames(const NativeFrameRouting&) {
    PERFETTO_FATAL("Aggregate operator does not support frame merging");
  }
  virtual base::Status Finalize() { return base::OkStatus(); }
  virtual core::Tree::Column TakeOutput() = 0;
};

AggregateOperator::~AggregateOperator() = default;

template <typename T>
class SumAggregateOperator final : public AggregateOperator {
 public:
  SumAggregateOperator(const core::Tree::Column& input, uint32_t output_rows)
      : input_(input),
        frame_totals_(core::Tree::Column::Create<T>(
            static_cast<uint32_t>(input.unchecked_span<T>().size()))),
        output_(core::Tree::Column::Create<T>(output_rows)) {
    frame_totals_.null_bv = core::BitVector::CreateWithSize(
        input.unchecked_span<T>().size(), false);
    output_.null_bv = core::BitVector::CreateWithSize(output_rows, false);
  }

  InputMode input_mode() const override {
    return InputMode::kNativeRowsToFramesThenFramesToOutputs;
  }

  base::Status UpdateFrames(const NativeFrameRouting& routing) override {
    core::Span<const T> input = input_.unchecked_span<T>();
    core::Span<T> totals = frame_totals_.unchecked_span<T>();
    for (uint32_t row = 0; row < input.size(); ++row) {
      if (input_.null_bv.size() > 0 && !input_.null_bv.is_set(row)) {
        continue;
      }
      const PathState& state = routing.states[row];
      if (state.retained_frame == core::Tree::kNullParent ||
          state.StackIsHidden()) {
        continue;
      }
      if (!AccumulateSum(input[row], state.retained_frame, totals,
                         &frame_totals_.null_bv)) {
        return base::ErrStatus("flamegraph: integer aggregate overflow");
      }
    }
    return base::OkStatus();
  }

  base::Status MergeFrames(const NativeFrameRouting& routing) override {
    const core::Tree::Column& frame_totals = frame_totals_;
    core::Span<const T> totals = frame_totals.unchecked_span<T>();
    core::Span<T> output = output_.unchecked_span<T>();
    if (routing.has_downward) {
      for (uint32_t frame = 0; frame < routing.states.size(); ++frame) {
        const PathState& state = routing.states[frame];
        if (!state.IsRetained(frame) || !frame_totals_.null_bv.is_set(frame) ||
            routing.downward_nodes[frame] == core::Tree::kNullParent ||
            !state.StackContributes(routing.required_show_stack_bits)) {
          continue;
        }
        if (!AccumulateSum(totals[frame], routing.downward_nodes[frame], output,
                           &output_.null_bv)) {
          return base::ErrStatus("flamegraph: integer aggregate overflow");
        }
      }
    }
    for (const TreeConstituent& constituent : routing.upward_constituents) {
      if (!frame_totals_.null_bv.is_set(constituent.frame)) {
        continue;
      }
      if (!AccumulateSum(totals[constituent.frame],
                         routing.downward_size + constituent.node, output,
                         &output_.null_bv)) {
        return base::ErrStatus("flamegraph: integer aggregate overflow");
      }
    }
    return base::OkStatus();
  }

  core::Tree::Column TakeOutput() override { return std::move(output_); }

 private:
  const core::Tree::Column& input_;
  core::Tree::Column frame_totals_;
  core::Tree::Column output_;
};

template <typename T>
class OneOrSummaryAggregateOperator final : public AggregateOperator {
 public:
  OneOrSummaryAggregateOperator(const core::Tree::Column& input,
                                StringPool& pool,
                                uint32_t output_rows)
      : input_(input),
        pool_(pool),
        output_(core::Tree::Column::Create<StringPool::Id>(output_rows)) {
    output_.null_bv = core::BitVector::CreateWithSize(output_rows, false);
  }

  base::Status UpdateBatch(core::Span<const uint32_t> output_rows,
                           core::Span<const uint32_t> input_rows) override {
    core::Span<const T> input = input_.unchecked_span<T>();
    for (uint32_t i = 0; i < input_rows.size(); ++i) {
      const uint32_t input_row = input_rows[i];
      if (input_.null_bv.size() > 0 && !input_.null_bv.is_set(input_row)) {
        continue;
      }
      if (output_rows[i] != current_output_row_) {
        FinishGroup();
        current_output_row_ = output_rows[i];
      }
      Add(input[input_row]);
    }
    return base::OkStatus();
  }

  base::Status Finalize() override {
    FinishGroup();
    return base::OkStatus();
  }

  core::Tree::Column TakeOutput() override { return std::move(output_); }

 private:
  void Add(T value) {
    const uint64_t key = DistinctKey(value);
    if (!has_value_) {
      representative_ = value;
      representative_key_ = key;
      has_value_ = true;
    } else if (!has_multiple_values_ && key != representative_key_) {
      distinct_keys_.push_back(representative_key_);
      distinct_keys_.push_back(key);
      has_multiple_values_ = true;
    } else if (has_multiple_values_) {
      distinct_keys_.push_back(key);
    }
  }

  void FinishGroup() {
    if (!has_value_) {
      return;
    }
    core::Span<StringPool::Id> output =
        output_.unchecked_span<StringPool::Id>();
    if constexpr (std::is_same_v<T, StringPool::Id>) {
      if (!has_multiple_values_) {
        output[current_output_row_] = representative_;
        output_.null_bv.set(current_output_row_);
        ResetGroup();
        return;
      }
    }
    summary_.clear();
    AppendValue(representative_, pool_, &summary_);
    if (has_multiple_values_) {
      std::sort(distinct_keys_.begin(), distinct_keys_.end());
      const size_t distinct_count = static_cast<size_t>(std::distance(
          distinct_keys_.begin(),
          std::unique(distinct_keys_.begin(), distinct_keys_.end())));
      summary_ += " and ";
      summary_ += std::to_string(distinct_count);
      summary_ += " others";
    }
    output[current_output_row_] =
        pool_.InternString(base::StringView(summary_));
    output_.null_bv.set(current_output_row_);
    ResetGroup();
  }

  void ResetGroup() {
    distinct_keys_.clear();
    has_value_ = false;
    has_multiple_values_ = false;
  }

  const core::Tree::Column& input_;
  StringPool& pool_;
  core::Tree::Column output_;
  std::vector<uint64_t> distinct_keys_;
  std::string summary_;
  T representative_{};
  uint64_t representative_key_ = 0;
  uint32_t current_output_row_ = core::Tree::kNullParent;
  bool has_value_ = false;
  bool has_multiple_values_ = false;
};

template <typename T>
class ConcatAggregateOperator final : public AggregateOperator {
 public:
  ConcatAggregateOperator(const core::Tree::Column& input,
                          StringPool& pool,
                          uint32_t output_rows)
      : input_(input),
        pool_(pool),
        output_(core::Tree::Column::Create<StringPool::Id>(output_rows)) {
    output_.null_bv = core::BitVector::CreateWithSize(output_rows, false);
  }

  base::Status UpdateBatch(core::Span<const uint32_t> output_rows,
                           core::Span<const uint32_t> input_rows) override {
    core::Span<const T> input = input_.unchecked_span<T>();
    for (uint32_t i = 0; i < input_rows.size(); ++i) {
      const uint32_t input_row = input_rows[i];
      if (input_.null_bv.size() > 0 && !input_.null_bv.is_set(input_row)) {
        continue;
      }
      if (output_rows[i] != current_output_row_) {
        FinishGroup();
        current_output_row_ = output_rows[i];
      }
      if (!text_.empty()) {
        text_.push_back(',');
      }
      AppendValue(input[input_row], pool_, &text_);
    }
    return base::OkStatus();
  }

  base::Status Finalize() override {
    FinishGroup();
    return base::OkStatus();
  }

  core::Tree::Column TakeOutput() override { return std::move(output_); }

 private:
  void FinishGroup() {
    if (text_.empty()) {
      return;
    }
    output_.unchecked_span<StringPool::Id>()[current_output_row_] =
        pool_.InternString(base::StringView(text_));
    output_.null_bv.set(current_output_row_);
    text_.clear();
  }

  const core::Tree::Column& input_;
  StringPool& pool_;
  core::Tree::Column output_;
  std::string text_;
  uint32_t current_output_row_ = core::Tree::kNullParent;
};

struct PackedTree {
  explicit PackedTree(uint32_t capacity)
      : depths(core::Slab<int64_t>::Alloc(capacity)),
        source_nodes(core::Slab<uint32_t>::Alloc(capacity)),
        parents(core::Slab<uint32_t>::Alloc(capacity)),
        representative_frames(core::Slab<uint32_t>::Alloc(capacity)) {}

  void Append(int64_t depth,
              uint32_t source_node,
              uint32_t parent,
              uint32_t representative_frame) {
    PERFETTO_DCHECK(rows < depths.size());
    depths[rows] = depth;
    source_nodes[rows] = source_node;
    parents[rows] = parent;
    representative_frames[rows] = representative_frame;
    ++rows;
  }

  uint32_t size() const { return rows; }
  core::Span<const uint32_t> source_node_span() const {
    return source_nodes.span().subspan(0, rows);
  }
  core::Span<const uint32_t> representative_frame_span() const {
    return representative_frames.span().subspan(0, rows);
  }

  core::Slab<int64_t> depths;
  core::Slab<uint32_t> source_nodes;
  core::Slab<uint32_t> parents;
  core::Slab<uint32_t> representative_frames;
  uint32_t rows = 0;
};

// Owns all temporary state for one flamegraph computation. The methods are the
// algorithm phases in execution order; helpers are outlined below to keep the
// class declaration readable.
class FlamegraphBuilder {
 public:
  FlamegraphBuilder(const core::Tree& input, const Config& config);

  base::StatusOr<core::Tree> Run();

 private:
  FilterMatches MatchText(const char* value) const;

  template <typename T>
  FilterMatches MatchNumber(T value) const;

  FilterMatches MatchColumn(const core::Tree::Column& column,
                            uint32_t row) const;
  FilterMatches MatchFrame(uint32_t row) const;
  void PrepareFrameHashes();
  void AnalyzePaths();
  void BuildDownwardHalf();
  base::Status BuildUpwardHalf();
  UpwardPath BuildUpwardPath(uint32_t frame, bool include_aggregates);
  bool ContributesToDownwardCumulative(uint32_t row) const;
  base::Status AccumulateMetrics();
  void BuildAggregateRouting();
  base::Status RunAggregateOperator(AggregateOperator* op);
  base::Status ComputeAggregates();
  base::StatusOr<core::Tree> PackOutput();

  template <typename T>
  base::Status MarkActiveBottomUpAnchors(const core::Tree::Column& column,
                                         core::Span<uint8_t> active);

  template <typename T>
  static void InitializeMetricColumns(uint32_t rows, MetricColumns* output);

  template <typename T>
  base::Status AccumulateMetric(const core::Tree::Column& input,
                                MetricColumns* output);

  template <typename T>
  base::Status AccumulateDownwardMetric(const core::Tree::Column& input,
                                        core::Span<T> self,
                                        core::Span<T> cumulative);

  template <typename T>
  base::Status AccumulateUpwardMetric(const core::Tree::Column& input,
                                      core::Span<T> self,
                                      core::Span<T> cumulative);

  template <typename T>
  base::Status AccumulateBottomUpMetric(const core::Tree::Column& input,
                                        core::Span<T> self,
                                        core::Span<T> cumulative);

  template <typename T>
  base::Status AccumulatePivotUpwardMetric(const core::Tree::Column& input,
                                           core::Span<T> self,
                                           core::Span<T> cumulative);

  template <typename T>
  static base::Status PropagateCumulative(const core::TreePathInterner& tree,
                                          core::Span<T> cumulative);

  void AppendPackedNodes(const core::TreePathInterner& tree,
                         uint32_t id_offset,
                         bool upward,
                         PackedTree* output) const;

  const core::Tree& input_;
  const Config& config_;
  uint64_t required_show_stack_bits_;

  PathAnalysis analysis_;
  DownwardHalf downward_;
  UpwardHalf upward_;
  AggregateRouting aggregate_routing_;
  std::vector<MetricColumns> metrics_;
  std::vector<core::Tree::Column> aggregate_columns_;
};

bool IsValidConfig(const core::Tree& input, const Config& config) {
  if (!config.name || !config.name->type.Is<core::String>() ||
      config.value_columns.empty()) {
    return false;
  }
  for (const core::Tree::Column* value : config.value_columns) {
    if (!value || !IsNumericColumn(*value)) {
      return false;
    }
  }

  std::vector<std::string> output_names{"depth", "name"};
  const auto add_output_name = [&](std::string name) {
    if (name.empty() || std::find(output_names.begin(), output_names.end(),
                                  name) != output_names.end()) {
      return false;
    }
    output_names.push_back(std::move(name));
    return true;
  };
  for (const core::Tree::Column* grouping : config.grouping_columns) {
    if (!grouping ||
        !add_output_name(std::string(input.ColumnName(grouping)))) {
      return false;
    }
  }
  for (const core::Tree::Column* value : config.value_columns) {
    const std::string name(input.ColumnName(value));
    if (!add_output_name("self_" + name) ||
        !add_output_name("cumulative_" + name)) {
      return false;
    }
  }
  for (const Config::AggregateColumn& aggregate : config.aggregate_columns) {
    if (!aggregate.input || !add_output_name(aggregate.output_name)) {
      return false;
    }
    if (aggregate.input->type.Is<core::Null>()) {
      // A column with no values aggregates to no values under any mode.
    } else if (aggregate.aggregate == Config::Aggregate::kSum &&
               !IsNumericColumn(*aggregate.input)) {
      return false;
    }
  }
  if (config.show_stack_filters.size() > Config::kMaxShowStackFilters) {
    return false;
  }
  const bool is_pattern_view = config.view.IsAnyOf<Config::PatternViews>();
  return is_pattern_view == config.view_pattern.has_value();
}

bool PathState::IsRetained(uint32_t row) const {
  return retained_frame == row;
}

bool PathState::StackIsHidden() const {
  return (stack_bits & kHideStackBit) != 0;
}

bool PathState::StackContributes(uint64_t required_show_stack_bits) const {
  return retained_frame != core::Tree::kNullParent &&
         stack_bits == required_show_stack_bits;
}

bool FlamegraphBuilder::ContributesToDownwardCumulative(uint32_t row) const {
  return downward_.node[row] != core::Tree::kNullParent &&
         analysis_.states[row].StackContributes(required_show_stack_bits_);
}

FlamegraphBuilder::FlamegraphBuilder(const core::Tree& input,
                                     const Config& config)
    : input_(input),
      config_(config),
      required_show_stack_bits_(
          (uint64_t{1} << config.show_stack_filters.size()) - 1),
      downward_(config.view.Is<Config::TopDown>() ? input.row_count : 0),
      upward_(config.view.Is<Config::BottomUp>() ? input.row_count : 0) {}

base::StatusOr<core::Tree> FlamegraphBuilder::Run() {
  AnalyzePaths();
  BuildDownwardHalf();
  RETURN_IF_ERROR(BuildUpwardHalf());
  analysis_.frame_hashes.clear();
  analysis_.frame_hashes.shrink_to_fit();
  RETURN_IF_ERROR(AccumulateMetrics());
  upward_.bottom_up_paths = {};
  upward_.pivot_anchors = {};
  RETURN_IF_ERROR(ComputeAggregates());
  analysis_.states = {};
  upward_.aggregate_constituents = {};
  return PackOutput();
}

FilterMatches FlamegraphBuilder::MatchText(const char* value) const {
  FilterMatches matches{};
  if (config_.view_pattern) {
    matches.view_pattern = config_.view_pattern->PartialMatch(value);
  }
  for (size_t i = 0; i < config_.show_stack_filters.size(); ++i) {
    if (config_.show_stack_filters[i].PartialMatch(value)) {
      matches.show_stack |= uint64_t{1} << i;
    }
  }
  for (const base::Regex& filter : config_.hide_stack_filters) {
    if (filter.PartialMatch(value)) {
      matches.hide_stack = 1;
      break;
    }
  }
  for (const base::Regex& filter : config_.hide_frame_filters) {
    if (filter.PartialMatch(value)) {
      matches.hide_frame = 1;
      break;
    }
  }
  return matches;
}

void FlamegraphBuilder::PrepareFrameHashes() {
  analysis_.frame_hashes.resize(input_.row_count);
  core::Span<base::MurmurHashCombiner> hashes =
      core::MakeMutableSpan(analysis_.frame_hashes);
  core::tree_ops::UpdateRowHashes(*config_.name, hashes);
  for (const core::Tree::Column* column : config_.grouping_columns) {
    core::tree_ops::UpdateRowHashes(*column, hashes);
  }
}

template <typename T>
FilterMatches FlamegraphBuilder::MatchNumber(T value) const {
  if constexpr (std::is_same_v<T, int64_t>) {
    return MatchText(base::StackString<32>("%" PRId64, value).c_str());
  } else {
    return MatchText(base::StackString<32>("%.15g", value).c_str());
  }
}

FilterMatches FlamegraphBuilder::MatchColumn(const core::Tree::Column& column,
                                             uint32_t row) const {
  if (column.null_bv.size() > 0 && !column.null_bv.is_set(row)) {
    return kNoFilterMatches;
  }
  if (column.type.Is<core::String>()) {
    const StringPool::Id value = column.unchecked_data<StringPool::Id>()[row];
    return value.is_null() ? kNoFilterMatches
                           : MatchText(config_.pool.Get(value).c_str());
  }
  if (column.type.Is<core::Int64>()) {
    return MatchNumber(column.unchecked_data<int64_t>()[row]);
  }
  return MatchNumber(column.unchecked_data<double>()[row]);
}

FilterMatches FlamegraphBuilder::MatchFrame(uint32_t row) const {
  FilterMatches matches = MatchColumn(*config_.name, row);
  for (const core::Tree::Column* column : config_.grouping_columns) {
    const FilterMatches column_matches = MatchColumn(*column, row);
    matches.show_stack |= column_matches.show_stack;
    matches.view_pattern |= column_matches.view_pattern;
    matches.hide_stack |= column_matches.hide_stack;
    matches.hide_frame |= column_matches.hide_frame;
  }
  return matches;
}

void FlamegraphBuilder::AnalyzePaths() {
  PrepareFrameHashes();
  analysis_.states = core::Slab<PathState>::Alloc(input_.row_count);
  if (config_.view_pattern) {
    analysis_.view_pattern_matches =
        core::BitVector::CreateWithSize(input_.row_count, false);
  }
  const bool needs_matching = config_.HasFilters() || config_.view_pattern;
  base::FlatHashMapV2<uint64_t, uint32_t, base::AlreadyHashed<uint64_t>>
      frame_match_index;
  base::FlatHashMapV2<StringPool::Id, uint32_t> name_match_index;
  std::vector<FilterMatches> match_pool;
  const StringPool::Id* names = config_.name->unchecked_data<StringPool::Id>();

  for (uint32_t row = 0; row < input_.row_count; ++row) {
    // Tree guarantees parents precede children, so inheritance is an O(1)
    // reference rather than an ancestor walk or a copy. Roots inherit the
    // shared empty path.
    const uint32_t parent = input_.parent[row];
    const PathState& inheritance = parent == core::Tree::kNullParent
                                       ? kEmptyPathState
                                       : analysis_.states[parent];
    PERFETTO_DCHECK(parent == core::Tree::kNullParent || parent < row);

    // A HIDE_FRAME row folds into its parent's path and cannot affect stack
    // filtering.
    PathState& state = analysis_.states[row];
    const FilterMatches* matches = &kNoFilterMatches;
    if (needs_matching) {
      const uint32_t next_index = static_cast<uint32_t>(match_pool.size());
      uint32_t* index;
      bool inserted;
      StringPool::Id matched_name;
      if (config_.grouping_columns.empty()) {
        const bool is_null = config_.name->null_bv.size() > 0 &&
                             !config_.name->null_bv.is_set(row);
        matched_name = is_null ? StringPool::Id() : names[row];
        std::tie(index, inserted) =
            name_match_index.Insert(matched_name, next_index);
      } else {
        const uint64_t frame_hash = analysis_.frame_hashes[row].digest();
        std::tie(index, inserted) =
            frame_match_index.Insert(frame_hash, next_index);
      }
      if (inserted) {
        if (config_.grouping_columns.empty()) {
          match_pool.push_back(
              matched_name.is_null()
                  ? kNoFilterMatches
                  : MatchText(config_.pool.Get(matched_name).c_str()));
        } else {
          match_pool.push_back(MatchFrame(row));
        }
      }
      matches = &match_pool[*index];
    }
    if (matches->hide_frame) {
      state = inheritance;
      PERFETTO_DCHECK(!state.IsRetained(row));
      continue;
    }

    // Every other row is retained, even when it is outside the built halves.
    // This lets matching descendants start roots and lets upward paths walk
    // through retained ancestors.
    state.retained_frame = row;
    if (config_.view_pattern && matches->view_pattern) {
      analysis_.view_pattern_matches.set(row);
    }
    state.stack_bits = matches->hide_stack
                           ? kHideStackBit
                           : matches->show_stack | inheritance.stack_bits;
  }
}

void FlamegraphBuilder::BuildDownwardHalf() {
  if (!config_.view.IsAnyOf<Config::DownwardViews>()) {
    // Every row sits outside the downward half.
    downward_.node =
        AllocFilled<uint32_t>(input_.row_count, core::Tree::kNullParent);
    return;
  }
  downward_.node = core::Slab<uint32_t>::Alloc(input_.row_count);
  for (uint32_t row = 0; row < input_.row_count; ++row) {
    const uint32_t parent = input_.parent[row];
    const uint32_t parent_node = parent == core::Tree::kNullParent
                                     ? core::Tree::kNullParent
                                     : downward_.node[parent];
    const PathState& state = analysis_.states[row];
    if (!state.IsRetained(row)) {
      downward_.node[row] = parent_node;
      continue;
    }

    // Top-down starts at every retained root. Pivot and from-frame start only
    // at matching frames; nested matches deliberately start another root.
    // Bottom-up has no downward half and never starts one.
    const bool has_retained_parent =
        parent != core::Tree::kNullParent &&
        analysis_.states[parent].retained_frame != core::Tree::kNullParent;
    const bool is_top_down_root =
        config_.view.Is<Config::TopDown>() && !has_retained_parent;
    const bool starts_downward_tree =
        (config_.view_pattern && analysis_.view_pattern_matches.is_set(row)) ||
        is_top_down_root;
    if (!starts_downward_tree && parent_node == core::Tree::kNullParent) {
      downward_.node[row] = core::Tree::kNullParent;
      continue;
    }

    // Equivalent frames under the same merged parent share a node. The first
    // row which creates the node remains its representative.
    downward_.node[row] = downward_.tree.Intern(
        starts_downward_tree ? core::Tree::kNullParent : parent_node,
        analysis_.frame_hashes[row], row);
  }
}

template <typename T>
base::Status FlamegraphBuilder::MarkActiveBottomUpAnchors(
    const core::Tree::Column& column,
    core::Span<uint8_t> active) {
  core::Span<const T> values = column.unchecked_span<T>();
  for (uint32_t row = 0; row < input_.row_count; ++row) {
    if (column.null_bv.size() > 0 && !column.null_bv.is_set(row)) {
      continue;
    }
    const T value = values[row];
    if (value < 0) {
      return base::ErrStatus("flamegraph: value columns must be non-negative");
    }
    const PathState& state = analysis_.states[row];
    if (!base::IsZero(value) &&
        state.StackContributes(required_show_stack_bits_)) {
      active[state.retained_frame] = 1;
    }
  }
  return base::OkStatus();
}

base::Status FlamegraphBuilder::BuildUpwardHalf() {
  if (config_.view.Is<Config::Pivot>()) {
    // Anchors are the retained rows matching the view pattern, in row order.
    for (uint32_t row = 0; row < input_.row_count; ++row) {
      if (!analysis_.view_pattern_matches.is_set(row)) {
        continue;
      }
      const bool include_aggregates =
          !config_.aggregate_columns.empty() &&
          analysis_.states[row].StackContributes(required_show_stack_bits_);
      upward_.pivot_anchors.push_back(
          UpwardAnchor{row, BuildUpwardPath(row, include_aggregates)});
    }
    return base::OkStatus();
  }
  if (!config_.view.Is<Config::BottomUp>()) {
    return base::OkStatus();
  }

  auto active = AllocFilled<uint8_t>(input_.row_count, 0);
  for (const core::Tree::Column* column : config_.value_columns) {
    if (column->type.Is<core::Int64>()) {
      RETURN_IF_ERROR(
          MarkActiveBottomUpAnchors<int64_t>(*column, active.mutable_span()));
    } else {
      RETURN_IF_ERROR(
          MarkActiveBottomUpAnchors<double>(*column, active.mutable_span()));
    }
  }
  constexpr UpwardPath kNoPath{core::Tree::kNullParent,
                               core::Tree::kNullParent};
  upward_.bottom_up_paths = AllocFilled<UpwardPath>(input_.row_count, kNoPath);
  for (uint32_t row = 0; row < input_.row_count; ++row) {
    if (active[row]) {
      upward_.bottom_up_paths[row] = BuildUpwardPath(row, true);
    }
  }
  return base::OkStatus();
}

UpwardPath FlamegraphBuilder::BuildUpwardPath(uint32_t frame,
                                              bool include_aggregates) {
  UpwardPath result{core::Tree::kNullParent, core::Tree::kNullParent};
  uint32_t upward_parent = core::Tree::kNullParent;
  while (frame != core::Tree::kNullParent) {
    upward_parent = upward_.tree.Intern(upward_parent,
                                        analysis_.frame_hashes[frame], frame);
    if (result.root == core::Tree::kNullParent) {
      result.root = upward_parent;
    }
    if (include_aggregates) {
      upward_.aggregate_constituents.push_back(
          TreeConstituent{frame, upward_parent});
    }
    const uint32_t parent = input_.parent[frame];
    frame = parent == core::Tree::kNullParent
                ? core::Tree::kNullParent
                : analysis_.states[parent].retained_frame;
  }
  result.leaf = upward_parent;
  return result;
}

template <typename T>
void FlamegraphBuilder::InitializeMetricColumns(uint32_t rows,
                                                MetricColumns* output) {
  output->self = core::Tree::Column::Create<T>(rows);
  output->cumulative = core::Tree::Column::Create<T>(rows);
  core::Span<T> self = output->self.unchecked_span<T>();
  core::Span<T> cumulative = output->cumulative.unchecked_span<T>();
  std::fill(self.begin(), self.end(), T{});
  std::fill(cumulative.begin(), cumulative.end(), T{});
}

template <typename T>
base::Status FlamegraphBuilder::AccumulateMetric(
    const core::Tree::Column& input,
    MetricColumns* output) {
  const uint32_t downward_size = downward_.tree.size();
  const uint32_t upward_size = upward_.tree.size();
  InitializeMetricColumns<T>(downward_size + upward_size, output);
  core::Span<T> self = output->self.unchecked_span<T>();
  core::Span<T> cumulative = output->cumulative.unchecked_span<T>();
  RETURN_IF_ERROR(
      AccumulateDownwardMetric<T>(input, self.subspan(0, downward_size),
                                  cumulative.subspan(0, downward_size)));
  return AccumulateUpwardMetric<T>(
      input, self.subspan(downward_size, upward_size),
      cumulative.subspan(downward_size, upward_size));
}

template <typename T>
base::Status FlamegraphBuilder::PropagateCumulative(
    const core::TreePathInterner& tree,
    core::Span<T> cumulative) {
  PERFETTO_DCHECK(tree.size() == cumulative.size());
  for (uint32_t node = tree.size(); node-- > 0;) {
    const uint32_t parent = tree.parent(node);
    if (parent != core::Tree::kNullParent &&
        !TryAdd(cumulative[node], &cumulative[parent])) {
      return base::ErrStatus("flamegraph: integer value overflow");
    }
  }
  return base::OkStatus();
}

template <typename T>
base::Status FlamegraphBuilder::AccumulateDownwardMetric(
    const core::Tree::Column& input,
    core::Span<T> self,
    core::Span<T> cumulative) {
  if (!config_.view.IsAnyOf<Config::DownwardViews>()) {
    return base::OkStatus();
  }

  core::Span<const T> values = input.unchecked_span<T>();
  for (uint32_t row = 0; row < input_.row_count; ++row) {
    if (input.null_bv.size() > 0 && !input.null_bv.is_set(row)) {
      continue;
    }
    const T value = values[row];
    if (value < 0) {
      return base::ErrStatus("flamegraph: value columns must be non-negative");
    }

    const uint32_t node = downward_.node[row];
    if (node == core::Tree::kNullParent) {
      continue;
    }
    if (analysis_.states[row].StackContributes(required_show_stack_bits_) &&
        (!TryAdd(value, &self[node]) || !TryAdd(value, &cumulative[node]))) {
      return base::ErrStatus("flamegraph: integer value overflow");
    }
  }

  return PropagateCumulative(downward_.tree, cumulative);
}

template <typename T>
base::Status FlamegraphBuilder::AccumulateUpwardMetric(
    const core::Tree::Column& input,
    core::Span<T> self,
    core::Span<T> cumulative) {
  if (!config_.view.IsAnyOf<Config::UpwardViews>()) {
    return base::OkStatus();
  }
  return config_.view.Is<Config::BottomUp>()
             ? AccumulateBottomUpMetric<T>(input, self, cumulative)
             : AccumulatePivotUpwardMetric<T>(input, self, cumulative);
}

template <typename T>
base::Status FlamegraphBuilder::AccumulateBottomUpMetric(
    const core::Tree::Column& input,
    core::Span<T> self,
    core::Span<T> cumulative) {
  core::Span<const T> values = input.unchecked_span<T>();
  for (uint32_t row = 0; row < input_.row_count; ++row) {
    if (input.null_bv.size() > 0 && !input.null_bv.is_set(row)) {
      continue;
    }
    const T value = values[row];
    const PathState& state = analysis_.states[row];
    if (base::IsZero(value) ||
        !state.StackContributes(required_show_stack_bits_)) {
      continue;
    }
    const uint32_t retained = state.retained_frame;
    const UpwardPath& path = upward_.bottom_up_paths[retained];
    const uint32_t root = path.root;
    const uint32_t leaf = path.leaf;
    PERFETTO_DCHECK(root != core::Tree::kNullParent);
    PERFETTO_DCHECK(leaf != core::Tree::kNullParent);
    if (!TryAdd(value, &self[root]) || !TryAdd(value, &cumulative[leaf])) {
      return base::ErrStatus("flamegraph: integer value overflow");
    }
  }
  return PropagateCumulative(upward_.tree, cumulative);
}

template <typename T>
base::Status FlamegraphBuilder::AccumulatePivotUpwardMetric(
    const core::Tree::Column& input,
    core::Span<T> self,
    core::Span<T> cumulative) {
  core::Span<const T> values = input.unchecked_span<T>();

  auto anchor_weights = AllocFilled<T>(input_.row_count, T{});
  for (uint32_t row = 0; row < input_.row_count; ++row) {
    if (input.null_bv.size() > 0 && !input.null_bv.is_set(row)) {
      continue;
    }
    const PathState& state = analysis_.states[row];
    if (ContributesToDownwardCumulative(row) &&
        !TryAdd(values[row], &anchor_weights[state.retained_frame])) {
      return base::ErrStatus("flamegraph: integer value overflow");
    }
  }

  for (uint32_t row = input_.row_count; row-- > 0;) {
    const PathState& state = analysis_.states[row];
    const uint32_t node = downward_.node[row];
    if (!state.IsRetained(row) || node == core::Tree::kNullParent ||
        downward_.tree.parent(node) == core::Tree::kNullParent) {
      continue;
    }
    const uint32_t parent = input_.parent[row];
    PERFETTO_DCHECK(parent != core::Tree::kNullParent);
    const uint32_t retained_parent = analysis_.states[parent].retained_frame;
    PERFETTO_DCHECK(retained_parent != core::Tree::kNullParent);
    if (!TryAdd(anchor_weights[row], &anchor_weights[retained_parent])) {
      return base::ErrStatus("flamegraph: integer value overflow");
    }
  }

  for (const UpwardAnchor& anchor : upward_.pivot_anchors) {
    const T value = anchor_weights[anchor.frame];
    if (!TryAdd(value, &self[anchor.path.root]) ||
        !TryAdd(value, &cumulative[anchor.path.leaf])) {
      return base::ErrStatus("flamegraph: integer value overflow");
    }
  }
  return PropagateCumulative(upward_.tree, cumulative);
}

base::Status FlamegraphBuilder::AccumulateMetrics() {
  metrics_.resize(config_.value_columns.size());
  for (uint32_t i = 0; i < config_.value_columns.size(); ++i) {
    const core::Tree::Column& column = *config_.value_columns[i];
    switch (column.type.index()) {
      case core::Tree::Column::Type::GetTypeIndex<core::Int64>():
        RETURN_IF_ERROR(AccumulateMetric<int64_t>(column, &metrics_[i]));
        break;
      case core::Tree::Column::Type::GetTypeIndex<core::Double>():
        RETURN_IF_ERROR(AccumulateMetric<double>(column, &metrics_[i]));
        break;
      default:
        PERFETTO_FATAL("Unsupported flamegraph value column type");
    }
  }
  return base::OkStatus();
}

void FlamegraphBuilder::BuildAggregateRouting() {
  const uint32_t rows = input_.row_count;
  aggregate_routing_.frame_rows_are_identity = true;
  for (uint32_t row = 0; row < rows; ++row) {
    const PathState& state = analysis_.states[row];
    if (state.retained_frame != row || state.StackIsHidden()) {
      aggregate_routing_.frame_rows_are_identity = false;
      break;
    }
  }
  if (!aggregate_routing_.frame_rows_are_identity) {
    aggregate_routing_.frame_row_offsets =
        core::Slab<uint32_t>::Alloc(uint64_t{rows} + 1);
    std::fill(aggregate_routing_.frame_row_offsets.begin(),
              aggregate_routing_.frame_row_offsets.end(), 0u);
    uint32_t routed_rows = 0;
    for (uint32_t row = 0; row < rows; ++row) {
      const PathState& state = analysis_.states[row];
      if (state.retained_frame != core::Tree::kNullParent &&
          !state.StackIsHidden()) {
        aggregate_routing_.frame_row_offsets[state.retained_frame + 1]++;
        routed_rows++;
      }
    }
    for (uint32_t frame = 0; frame < rows; ++frame) {
      aggregate_routing_.frame_row_offsets[frame + 1] +=
          aggregate_routing_.frame_row_offsets[frame];
    }
    aggregate_routing_.frame_rows = core::Slab<uint32_t>::Alloc(routed_rows);
    core::Slab<uint32_t> frame_fill = core::Slab<uint32_t>::Alloc(rows);
    memcpy(frame_fill.data(), aggregate_routing_.frame_row_offsets.data(),
           rows * sizeof(uint32_t));
    for (uint32_t row = 0; row < rows; ++row) {
      const PathState& state = analysis_.states[row];
      if (state.retained_frame != core::Tree::kNullParent &&
          !state.StackIsHidden()) {
        aggregate_routing_.frame_rows[frame_fill[state.retained_frame]++] = row;
      }
    }
  }

  const uint32_t downward_size = downward_.tree.size();
  const uint32_t nodes = downward_size + upward_.tree.size();
  aggregate_routing_.output_frame_offsets =
      core::Slab<uint32_t>::Alloc(uint64_t{nodes} + 1);
  std::fill(aggregate_routing_.output_frame_offsets.begin(),
            aggregate_routing_.output_frame_offsets.end(), 0u);
  uint32_t routed_frames = 0;
  if (config_.view.IsAnyOf<Config::DownwardViews>()) {
    for (uint32_t frame = 0; frame < rows; ++frame) {
      if (analysis_.states[frame].IsRetained(frame) &&
          ContributesToDownwardCumulative(frame)) {
        aggregate_routing_.output_frame_offsets[downward_.node[frame] + 1]++;
        routed_frames++;
      }
    }
  }
  for (const TreeConstituent& constituent : upward_.aggregate_constituents) {
    aggregate_routing_
        .output_frame_offsets[downward_size + constituent.node + 1]++;
    routed_frames++;
  }
  for (uint32_t node = 0; node < nodes; ++node) {
    aggregate_routing_.output_frame_offsets[node + 1] +=
        aggregate_routing_.output_frame_offsets[node];
  }
  aggregate_routing_.output_frames = core::Slab<uint32_t>::Alloc(routed_frames);
  core::Slab<uint32_t> output_fill = core::Slab<uint32_t>::Alloc(nodes);
  memcpy(output_fill.data(), aggregate_routing_.output_frame_offsets.data(),
         nodes * sizeof(uint32_t));
  if (config_.view.IsAnyOf<Config::DownwardViews>()) {
    for (uint32_t frame = 0; frame < rows; ++frame) {
      if (analysis_.states[frame].IsRetained(frame) &&
          ContributesToDownwardCumulative(frame)) {
        const uint32_t node = downward_.node[frame];
        aggregate_routing_.output_frames[output_fill[node]++] = frame;
      }
    }
  }
  for (const TreeConstituent& constituent : upward_.aggregate_constituents) {
    const uint32_t node = downward_size + constituent.node;
    aggregate_routing_.output_frames[output_fill[node]++] = constituent.frame;
  }
}

base::Status FlamegraphBuilder::RunAggregateOperator(AggregateOperator* op) {
  constexpr size_t kBatchSize = 4096;
  std::vector<uint32_t> destinations;
  std::vector<uint32_t> sources;
  destinations.reserve(kBatchSize);
  sources.reserve(kBatchSize);

  if (op->input_mode() ==
      AggregateOperator::InputMode::kNativeRowsToFramesThenFramesToOutputs) {
    const core::Slab<PathState>& states = analysis_.states;
    const core::Slab<uint32_t>& downward_nodes = downward_.node;
    const core::FlexVector<TreeConstituent>& upward_constituents =
        upward_.aggregate_constituents;
    NativeFrameRouting routing{
        states.span(),
        downward_nodes.span(),
        upward_constituents.span(),
        downward_.tree.size(),
        required_show_stack_bits_,
        config_.view.IsAnyOf<Config::DownwardViews>(),
    };
    RETURN_IF_ERROR(op->UpdateFrames(routing));
    RETURN_IF_ERROR(op->MergeFrames(routing));
    return op->Finalize();
  }

  const uint32_t nodes = downward_.tree.size() + upward_.tree.size();
  for (uint32_t node = 0; node < nodes; ++node) {
    for (uint32_t fi = aggregate_routing_.output_frame_offsets[node];
         fi < aggregate_routing_.output_frame_offsets[node + 1]; ++fi) {
      const uint32_t frame = aggregate_routing_.output_frames[fi];
      if (aggregate_routing_.frame_rows_are_identity) {
        destinations.push_back(node);
        sources.push_back(frame);
      } else {
        for (uint32_t ri = aggregate_routing_.frame_row_offsets[frame];
             ri < aggregate_routing_.frame_row_offsets[frame + 1]; ++ri) {
          destinations.push_back(node);
          sources.push_back(aggregate_routing_.frame_rows[ri]);
          if (sources.size() == kBatchSize) {
            RETURN_IF_ERROR(op->UpdateBatch(core::MakeSpan(destinations),
                                            core::MakeSpan(sources)));
            destinations.clear();
            sources.clear();
          }
        }
      }
      if (sources.size() == kBatchSize) {
        RETURN_IF_ERROR(op->UpdateBatch(core::MakeSpan(destinations),
                                        core::MakeSpan(sources)));
        destinations.clear();
        sources.clear();
      }
    }
  }
  if (!sources.empty()) {
    RETURN_IF_ERROR(
        op->UpdateBatch(core::MakeSpan(destinations), core::MakeSpan(sources)));
  }
  return op->Finalize();
}

base::Status FlamegraphBuilder::ComputeAggregates() {
  bool needs_materialized_routing = false;
  for (const Config::AggregateColumn& aggregate : config_.aggregate_columns) {
    needs_materialized_routing |=
        aggregate.aggregate != Config::Aggregate::kSum;
  }
  if (needs_materialized_routing) {
    BuildAggregateRouting();
  }
  const uint32_t nodes = downward_.tree.size() + upward_.tree.size();
  aggregate_columns_.resize(config_.aggregate_columns.size());
  for (uint32_t i = 0; i < config_.aggregate_columns.size(); ++i) {
    const Config::AggregateColumn& aggregate = config_.aggregate_columns[i];
    const core::Tree::Column& input = *aggregate.input;
    if (input.type.Is<core::Null>()) {
      aggregate_columns_[i] = core::Tree::Column::CreateNull(nodes);
      continue;
    }
    switch (aggregate.aggregate) {
      case Config::Aggregate::kSum:
        if (input.type.Is<core::Int64>()) {
          SumAggregateOperator<int64_t> op(input, nodes);
          RETURN_IF_ERROR(RunAggregateOperator(&op));
          aggregate_columns_[i] = op.TakeOutput();
        } else {
          SumAggregateOperator<double> op(input, nodes);
          RETURN_IF_ERROR(RunAggregateOperator(&op));
          aggregate_columns_[i] = op.TakeOutput();
        }
        break;
      case Config::Aggregate::kOneOrSummary:
        if (input.type.Is<core::String>()) {
          OneOrSummaryAggregateOperator<StringPool::Id> op(input, config_.pool,
                                                           nodes);
          RETURN_IF_ERROR(RunAggregateOperator(&op));
          aggregate_columns_[i] = op.TakeOutput();
        } else if (input.type.Is<core::Int64>()) {
          OneOrSummaryAggregateOperator<int64_t> op(input, config_.pool, nodes);
          RETURN_IF_ERROR(RunAggregateOperator(&op));
          aggregate_columns_[i] = op.TakeOutput();
        } else {
          OneOrSummaryAggregateOperator<double> op(input, config_.pool, nodes);
          RETURN_IF_ERROR(RunAggregateOperator(&op));
          aggregate_columns_[i] = op.TakeOutput();
        }
        break;
      case Config::Aggregate::kConcatWithComma:
        if (input.type.Is<core::String>()) {
          ConcatAggregateOperator<StringPool::Id> op(input, config_.pool,
                                                     nodes);
          RETURN_IF_ERROR(RunAggregateOperator(&op));
          aggregate_columns_[i] = op.TakeOutput();
        } else if (input.type.Is<core::Int64>()) {
          ConcatAggregateOperator<int64_t> op(input, config_.pool, nodes);
          RETURN_IF_ERROR(RunAggregateOperator(&op));
          aggregate_columns_[i] = op.TakeOutput();
        } else {
          ConcatAggregateOperator<double> op(input, config_.pool, nodes);
          RETURN_IF_ERROR(RunAggregateOperator(&op));
          aggregate_columns_[i] = op.TakeOutput();
        }
        break;
    }
  }
  return base::OkStatus();
}

void FlamegraphBuilder::AppendPackedNodes(const core::TreePathInterner& tree,
                                          uint32_t id_offset,
                                          bool upward,
                                          PackedTree* output) const {
  auto keep = AllocFilled<uint8_t>(tree.size(), 0);
  for (const MetricColumns& metric : metrics_) {
    UpdateNonZeroRows(metric.cumulative, id_offset, keep.mutable_span());
  }

  auto output_row = AllocFilled<uint32_t>(tree.size(), core::Tree::kNullParent);
  for (uint32_t node = 0; node < tree.size(); ++node) {
    if (!keep[node]) {
      continue;
    }
    const uint32_t parent = tree.parent(node);
    const uint32_t packed_parent = parent == core::Tree::kNullParent
                                       ? core::Tree::kNullParent
                                       : output_row[parent];
    PERFETTO_DCHECK(parent == core::Tree::kNullParent ||
                    packed_parent != core::Tree::kNullParent);
    const int64_t depth =
        packed_parent == core::Tree::kNullParent
            ? (upward ? -1 : 1)
            : output->depths[packed_parent] + (upward ? -1 : 1);
    output_row[node] = output->size();
    output->Append(depth, id_offset + node, packed_parent,
                   tree.representative_row(node));
  }
}

base::StatusOr<core::Tree> FlamegraphBuilder::PackOutput() {
  PackedTree packed(downward_.tree.size() + upward_.tree.size());
  AppendPackedNodes(downward_.tree, 0, false, &packed);
  AppendPackedNodes(upward_.tree, downward_.tree.size(), true, &packed);

  core::Tree output;
  output.row_count = packed.size();
  const size_t column_count = 2 + config_.grouping_columns.size() +
                              2 * config_.value_columns.size() +
                              config_.aggregate_columns.size();
  output.names.reserve(column_count);
  output.columns.reserve(column_count);

  // The packed arrays are already in output order: adopt them.
  packed.parents.Truncate(packed.size());
  output.parent = std::move(packed.parents);
  packed.depths.Truncate(packed.size());
  core::Tree::Column depth;
  depth.data = std::move(packed.depths).TakeAsBytes();
  output.names.push_back("depth");
  output.columns.push_back(std::move(depth));

  // TODO(lalitm): the copies below exist because every Tree operation
  // materializes a full rewritten tree for the next one to consume. If tree
  // operations composed as a linear pipeline of ops over the source instead,
  // the intermediate rewrites (and this packing step with them) would
  // largely disappear.
  const core::Span<const uint32_t> representatives =
      packed.representative_frame_span();
  output.names.push_back("name");
  output.columns.push_back(
      core::tree_ops::Gather(*config_.name, representatives));
  for (const core::Tree::Column* grouping : config_.grouping_columns) {
    output.names.emplace_back(input_.ColumnName(grouping));
    output.columns.push_back(
        core::tree_ops::Gather(*grouping, representatives));
  }

  // Kept nodes are appended in id order, so the packed sources are a strictly
  // increasing filter over the combined id space. When every node is kept the
  // filter is the identity and the builder-owned columns can be adopted
  // outright instead of gathered.
  const core::Span<const uint32_t> packed_sources = packed.source_node_span();
  const bool keep_all =
      packed.size() == downward_.tree.size() + upward_.tree.size();
  const auto take_or_gather = [&](core::Tree::Column& column) {
    return keep_all ? std::move(column)
                    : core::tree_ops::Gather(column, packed_sources);
  };
  for (uint32_t i = 0; i < config_.value_columns.size(); ++i) {
    const std::string name(input_.ColumnName(config_.value_columns[i]));
    output.names.push_back("self_" + name);
    output.columns.push_back(take_or_gather(metrics_[i].self));
    output.names.push_back("cumulative_" + name);
    output.columns.push_back(take_or_gather(metrics_[i].cumulative));
  }
  for (uint32_t i = 0; i < config_.aggregate_columns.size(); ++i) {
    output.names.push_back(config_.aggregate_columns[i].output_name);
    output.columns.push_back(take_or_gather(aggregate_columns_[i]));
  }
  return std::move(output);
}

}  // namespace

base::StatusOr<core::Tree> Build(const core::Tree& tree, const Config& config) {
  if (!IsValidConfig(tree, config)) {
    return base::ErrStatus("flamegraph: invalid configuration");
  }
  return FlamegraphBuilder(tree, config).Run();
}

namespace {}  // namespace

// Lays out siblings widest first: a stable radix sort orders all nodes by
// descending width, and distributing that order into per-parent buckets
// keeps it within every sibling list. A breadth-first walk then packs each
// node's children left-to-right from the node's own position; the walk
// order is the render order.
Layout ComputeLayout(const core::Tree& tree,
                     const core::Tree::Column& cumulative,
                     const core::Tree::Column& depth) {
  const uint32_t row_count = tree.row_count;
  if (row_count == 0) {
    return {};
  }
  const core::Span<const int64_t> depths = depth.unchecked_span<int64_t>();
  core::Slab<double> width = core::Slab<double>::Alloc(row_count);
  const bool cumulative_is_int = cumulative.type.Is<core::Int64>();
  for (uint32_t node = 0; node < row_count; ++node) {
    if (cumulative.null_bv.size() > 0 && !cumulative.null_bv.is_set(node)) {
      width[node] = 0;
      continue;
    }
    const double value =
        cumulative_is_int
            ? static_cast<double>(cumulative.unchecked_data<int64_t>()[node])
            : cumulative.unchecked_data<double>()[node];
    width[node] = std::max(value, 0.0);
  }

  // Order all nodes by descending width, ties in input order. The key is the
  // complemented big-endian IEEE representation, whose unsigned order matches
  // descending width for the non-negative widths used here.
  core::Slab<uint64_t> keys = core::Slab<uint64_t>::Alloc(row_count);
  core::Slab<uint32_t> by_width = core::Slab<uint32_t>::Alloc(row_count);
  for (uint32_t node = 0; node < row_count; ++node) {
    uint64_t bits;
    memcpy(&bits, &width[node], sizeof(bits));
    keys[node] = base::HostToBE64(~bits);
    by_width[node] = node;
  }
  core::Slab<uint32_t> scratch = core::Slab<uint32_t>::Alloc(row_count);
  core::Slab<uint32_t> radix_counts = core::Slab<uint32_t>::Alloc(1u << 16);
  const uint32_t* by_width_sorted = core::RadixSort(
      by_width.begin(), by_width.end(), scratch.begin(), radix_counts.data(),
      sizeof(uint64_t), [&](uint32_t node) {
        return reinterpret_cast<const uint8_t*>(&keys[node]);
      });

  // Distribute the width order into per-parent buckets. Roots go to one
  // strip per traversal direction, each starting at x = 0.
  uint32_t downward_root_count = 0;
  uint32_t upward_root_count = 0;
  core::Slab<uint32_t> child_offset =
      core::Slab<uint32_t>::Alloc(row_count + 1);
  std::fill(child_offset.begin(), child_offset.end(), 0u);
  for (uint32_t node = 0; node < row_count; ++node) {
    if (tree.parent[node] != core::Tree::kNullParent) {
      child_offset[tree.parent[node] + 1]++;
    } else if (depths[node] >= 0) {
      downward_root_count++;
    } else {
      upward_root_count++;
    }
  }
  for (uint32_t node = 0; node < row_count; ++node) {
    child_offset[node + 1] += child_offset[node];
  }
  core::Slab<uint32_t> child_fill = core::Slab<uint32_t>::Alloc(row_count);
  memcpy(child_fill.data(), child_offset.data(), row_count * sizeof(uint32_t));
  core::Slab<uint32_t> children =
      core::Slab<uint32_t>::Alloc(child_offset[row_count]);
  core::Slab<uint32_t> downward_roots =
      core::Slab<uint32_t>::Alloc(downward_root_count);
  core::Slab<uint32_t> upward_roots =
      core::Slab<uint32_t>::Alloc(upward_root_count);
  uint32_t downward_fill = 0;
  uint32_t upward_fill = 0;
  for (uint32_t i = 0; i < row_count; ++i) {
    const uint32_t node = by_width_sorted[i];
    const uint32_t parent = tree.parent[node];
    if (parent != core::Tree::kNullParent) {
      children[child_fill[parent]++] = node;
    } else if (depths[node] >= 0) {
      downward_roots[downward_fill++] = node;
    } else {
      upward_roots[upward_fill++] = node;
    }
  }

  core::Slab<double> x = core::Slab<double>::Alloc(row_count);
  double strip_x = 0;
  for (const uint32_t root : downward_roots) {
    x[root] = strip_x;
    strip_x += width[root];
  }
  strip_x = 0;
  for (const uint32_t root : upward_roots) {
    x[root] = strip_x;
    strip_x += width[root];
  }

  Layout layout;
  layout.node = core::Slab<uint32_t>::Alloc(row_count);
  layout.parent_row = core::Slab<uint32_t>::Alloc(row_count);
  layout.x_start = core::Slab<double>::Alloc(row_count);
  uint32_t tail = 0;
  const auto emit = [&](uint32_t node, uint32_t parent_row) {
    layout.node[tail] = node;
    layout.parent_row[tail] = parent_row;
    layout.x_start[tail] = x[node];
    tail++;
  };

  // Each strip is in x order, so merging them by position (the downward
  // strip first on ties) yields the roots' render order; the walk keeps it
  // level by level.
  for (uint32_t di = 0, ui = 0;
       di < downward_roots.size() || ui < upward_roots.size();) {
    if (ui >= upward_roots.size() ||
        (di < downward_roots.size() &&
         x[downward_roots[di]] <= x[upward_roots[ui]])) {
      emit(downward_roots[di++], core::Tree::kNullParent);
    } else {
      emit(upward_roots[ui++], core::Tree::kNullParent);
    }
  }
  for (uint32_t row = 0; row < tail; ++row) {
    const uint32_t node = layout.node[row];
    double offset = layout.x_start[row];
    for (uint32_t i = child_offset[node]; i < child_offset[node + 1]; ++i) {
      const uint32_t child = children[i];
      x[child] = offset;
      offset += width[child];
      emit(child, row);
    }
  }
  PERFETTO_DCHECK(tail == row_count);
  return layout;
}

}  // namespace perfetto::trace_processor::flamegraph
