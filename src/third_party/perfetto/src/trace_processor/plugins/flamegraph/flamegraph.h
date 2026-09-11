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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_FLAMEGRAPH_FLAMEGRAPH_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_FLAMEGRAPH_FLAMEGRAPH_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/ext/base/regex.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/tree/tree.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/type_set.h"

namespace perfetto::trace_processor::flamegraph {

inline bool IsNumericColumn(const core::Tree::Column& column) {
  return column.type.Is<core::Int64>() || column.type.Is<core::Double>();
}

// Fully resolved inputs to the flamegraph algorithm. The SQL layer is
// responsible for resolving column names, validating column types, and
// compiling patterns before calling Build. Per-row matching remains part of
// the algorithm.
// Column pointers refer to the Tree passed to Build and only need to remain
// valid for that call.
struct Config {
  explicit Config(StringPool& string_pool) : pool(string_pool) {}

  static constexpr uint32_t kMaxShowStackFilters = 61;

  // Structural orientations of the output. The named subsets below describe
  // which views share a behaviour; membership tests use View::IsAnyOf.
  struct TopDown {};
  struct BottomUp {};
  struct Pivot {};
  struct FromFrame {};
  using View = core::TypeSet<TopDown, BottomUp, Pivot, FromFrame>;

  // Views anchored on frames matching |view_pattern|.
  using PatternViews = core::TypeSet<Pivot, FromFrame>;
  // Views which build the downward (descendant) half of the output.
  using DownwardViews = core::TypeSet<TopDown, Pivot, FromFrame>;
  // Views which build the upward (ancestor) half of the output.
  using UpwardViews = core::TypeSet<BottomUp, Pivot>;

  enum class Aggregate {
    kSum,
    kOneOrSummary,
    kConcatWithComma,
  };

  struct AggregateColumn {
    const core::Tree::Column* input;
    Aggregate aggregate;
    std::string output_name;
  };

  StringPool& pool;

  // Structural orientation. Pivot builds both descendants and ancestors of
  // rows matching |view_pattern|. From-frame builds only descendants of the
  // matching rows. |view_pattern| is set exactly for those two views.
  View view = View(TopDown{});
  std::optional<base::Regex> view_pattern;

  // Frame identity. Grouping columns, together with name, determine which
  // frames can be merged. Regex filters match against all of these columns.
  const core::Tree::Column* name = nullptr;
  std::vector<const core::Tree::Column*> grouping_columns;

  // Filtering, separated by operation before entering the algorithm.
  std::vector<base::Regex> show_stack_filters;
  std::vector<base::Regex> hide_stack_filters;
  std::vector<base::Regex> hide_frame_filters;

  // Numeric sample-weight columns. Each is SUM-aggregated into a
  // self_<name>/cumulative_<name> output pair. At least one is required.
  std::vector<const core::Tree::Column*> value_columns;

  // Additional aggregates carried through structural folds. They do not
  // contribute to flamegraph geometry.
  std::vector<AggregateColumn> aggregate_columns;

  // Returns true if any filters are configured, false otherwise.
  bool HasFilters() const {
    return !show_stack_filters.empty() || !hide_stack_filters.empty() ||
           !hide_frame_filters.empty();
  }
};

// Builds a merged structural flamegraph from a parent-before-child tree.
// Presentation layout is a separate operation: see ComputeLayout.
base::StatusOr<core::Tree> Build(const core::Tree&, const Config&);

// Presentation geometry for one built flamegraph, in render order:
// breadth-first, siblings packed left-to-right by decreasing cumulative
// value. A node's width is its cumulative value clamped to zero, so its end
// position is x_start plus that width.
struct Layout {
  core::Slab<uint32_t> node;        // output row -> tree row
  core::Slab<uint32_t> parent_row;  // output row -> parent output row
  core::Slab<double> x_start;
};

// Computes presentation geometry for a tree returned by Build, positioning
// each node by |cumulative|, one of the tree's cumulative value columns.
// |depth| is the tree's depth column; its sign separates the downward and
// upward halves of the flamegraph, which are laid out as independent strips.
// Nodes with non-positive cumulative value take up no width but keep their
// position so that consumers of secondary value columns still see them.
Layout ComputeLayout(const core::Tree&,
                     const core::Tree::Column& cumulative,
                     const core::Tree::Column& depth);

}  // namespace perfetto::trace_processor::flamegraph

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_FLAMEGRAPH_FLAMEGRAPH_H_
