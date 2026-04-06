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

#include "src/trace_processor/core/tree/tree_transformer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/common/tree_types.h"
#include "src/trace_processor/core/common/value_fetcher.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/core/dataframe/cursor.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/query_plan.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/interpreter/bytecode_builder.h"
#include "src/trace_processor/core/interpreter/bytecode_instructions.h"
#include "src/trace_processor/core/interpreter/bytecode_interpreter.h"
#include "src/trace_processor/core/interpreter/bytecode_interpreter_impl.h"  // IWYU pragma: keep
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/range.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core::tree {
namespace {

struct IdCallback : core::dataframe::CellCallback {
  void OnCell(int64_t id) {
    id_value = id;
    type_ok = true;
  }
  void OnCell(double) { type_ok = false; }
  void OnCell(NullTermStringView) { type_ok = false; }
  void OnCell(std::nullptr_t) {
    id_value = std::nullopt;
    type_ok = true;
  }
  void OnCell(uint32_t id) {
    id_value = id;
    type_ok = true;
  }
  void OnCell(int32_t id) {
    id_value = id;
    type_ok = true;
  }
  std::optional<int64_t> id_value;
  bool type_ok = false;
};

// Builds a mapping from ID values to row indices.
// Returns an error if IDs are non-integer or null.
base::StatusOr<base::FlatHashMap<int64_t, uint32_t>> BuildIdToRowMap(
    const dataframe::Dataframe& df) {
  base::FlatHashMap<int64_t, uint32_t> id_to_row;
  IdCallback id_cb;
  for (uint32_t row = 0; row < df.row_count(); ++row) {
    df.GetCell(row, 0, id_cb);
    if (PERFETTO_UNLIKELY(!id_cb.type_ok)) {
      return base::ErrStatus("ID column has non-integer values");
    }
    if (PERFETTO_UNLIKELY(!id_cb.id_value.has_value())) {
      return base::ErrStatus("ID column has null values");
    }
    id_to_row[*id_cb.id_value] = row;
  }
  return std::move(id_to_row);
}

// Builds normalized parent storage where parent IDs are converted to row
// indices. Root nodes (null parent_id) get UINT32_MAX.
base::StatusOr<Slab<uint32_t>> BuildNormalizedParentStorage(
    const dataframe::Dataframe& df,
    const base::FlatHashMap<int64_t, uint32_t>& id_to_row) {
  uint32_t row_count = df.row_count();
  auto normalized_parent = Slab<uint32_t>::Alloc(row_count);
  IdCallback id_cb;
  for (uint32_t row = 0; row < row_count; ++row) {
    df.GetCell(row, 1, id_cb);  // Parent ID is column 1
    if (PERFETTO_UNLIKELY(!id_cb.type_ok)) {
      return base::ErrStatus("Parent ID column has non-integer values");
    }
    if (id_cb.id_value.has_value()) {
      auto* parent_row = id_to_row.Find(*id_cb.id_value);
      if (!parent_row) {
        return base::ErrStatus("Parent ID not found in ID column");
      }
      normalized_parent[row] = *parent_row;
    } else {
      normalized_parent[row] = kNullParent;
    }
  }
  return std::move(normalized_parent);
}

// Creates an AdhocDataframeBuilder configured for tree columns.
dataframe::AdhocDataframeBuilder MakeTreeColumnBuilder(StringPool* pool) {
  return dataframe::AdhocDataframeBuilder(
      {"_tree_id", "_tree_parent_id"}, pool,
      dataframe::AdhocDataframeBuilder::Options{
          {}, dataframe::NullabilityType::kDenseNull});
}

// Builds tree columns (_tree_id, _tree_parent_id) from parent data.
// parent_data[i] contains the parent row index for row i, or kNullParent for
// roots.
base::StatusOr<dataframe::Dataframe> BuildTreeColumns(
    const uint32_t* parent_data,
    uint32_t count,
    StringPool* pool) {
  auto builder = MakeTreeColumnBuilder(pool);
  for (uint32_t i = 0; i < count; ++i) {
    builder.PushNonNull(0, i);
    if (parent_data[i] == kNullParent) {
      builder.PushNull(1);
    } else {
      builder.PushNonNull(1, parent_data[i]);
    }
  }
  return std::move(builder).Build();
}

// Filter value fetcher for tree operations that returns stored SqlValues.
struct TreeValueFetcher : core::ValueFetcher {
  static const Type kInt64 = static_cast<Type>(SqlValue::Type::kLong);
  static const Type kDouble = static_cast<Type>(SqlValue::Type::kDouble);
  static const Type kString = static_cast<Type>(SqlValue::Type::kString);
  static const Type kNull = static_cast<Type>(SqlValue::Type::kNull);

  explicit TreeValueFetcher(const SqlValue* v) : values(v) {}

  Type GetValueType(uint32_t i) const {
    return static_cast<Type>(values[i].type);
  }
  int64_t GetInt64Value(uint32_t i) const { return values[i].AsLong(); }
  double GetDoubleValue(uint32_t i) const { return values[i].AsDouble(); }
  const char* GetStringValue(uint32_t i) const { return values[i].AsString(); }
  static bool IteratorInit(uint32_t) { return false; }
  static bool IteratorNext(uint32_t) { return false; }

  const SqlValue* values = nullptr;
};

}  // namespace

// =============================================================================
// Constructor
// =============================================================================

TreeTransformer::TreeTransformer(dataframe::Dataframe df, StringPool* pool)
    : df_(std::move(df)),
      pool_(pool),
      builder_(std::make_unique<interpreter::BytecodeBuilder>()),
      cache_(std::make_unique<dataframe::DataframeRegisterCache>(*builder_)) {}

// =============================================================================
// Public methods
// =============================================================================

base::Status TreeTransformer::FilterTree(
    std::vector<dataframe::FilterSpec> specs,
    std::vector<SqlValue> values) {
  uint32_t n = df_.row_count();
  if (n == 0 || specs.empty()) {
    return base::OkStatus();
  }

  // Initialize tree structure on first FilterTree call.
  // This allocates all scratch buffers once for reuse across filters.
  if (!filter_scratch_.has_value()) {
    InitializeTreeStructure(n);
  }

  // Store filter values and set value_index in specs.
  for (size_t i = 0; i < specs.size(); ++i) {
    specs[i].value_index = static_cast<uint32_t>(filter_values_.size());
    filter_values_.push_back(values[i]);
  }

  // Rebuild P2C from current C2P if stale.
  EnsureParentToChildStructure();

  // Build filter bitvector from specs.
  ASSIGN_OR_RETURN(auto keep_bv, BuildFilterBitvector(n, specs));

  // Emit the tree filter operation (updates C2P, invalidates P2C).
  EmitFilterTreeBytecode(keep_bv);
  p2c_stale_ = true;

  return base::OkStatus();
}

base::StatusOr<dataframe::Dataframe> TreeTransformer::ToDataframe() && {
  using StoragePtr = interpreter::StoragePtr;
  using SpanHandle = interpreter::ReadHandle<Span<uint32_t>>;

  ASSIGN_OR_RETURN(auto id_to_row, BuildIdToRowMap(df_));
  ASSIGN_OR_RETURN(auto normalized_parent,
                   BuildNormalizedParentStorage(df_, id_to_row));
  if (df_.row_count() == 0 || !filter_scratch_.has_value()) {
    ASSIGN_OR_RETURN(auto tree_cols, BuildTreeColumns(normalized_parent.begin(),
                                                      df_.row_count(), pool_));
    return dataframe::Dataframe::HorizontalConcat(std::move(tree_cols),
                                                  std::move(df_));
  }

  interpreter::Interpreter<TreeValueFetcher> interp;
  interp.Initialize(builder_->bytecode(), builder_->register_count(), pool_);
  interp.SetRegisterValue(
      interpreter::WriteHandle<StoragePtr>(parent_storage_reg_.index),
      StoragePtr{normalized_parent.begin(), StorageType(Uint32{})});

  for (const auto& init : register_inits_) {
    auto val = dataframe::QueryPlanImpl::GetRegisterInitValue(init, df_);
    interp.SetRegisterValue(interpreter::HandleBase{init.dest_register},
                            std::move(val));
  }

  TreeValueFetcher fetcher(filter_values_.data());
  interp.Execute(fetcher);

  // Get final parent and original_rows spans.
  const auto* parent_span =
      interp.GetRegisterValue(SpanHandle(parent_span_.index));
  const auto* original_rows_span =
      interp.GetRegisterValue(SpanHandle(original_rows_span_.index));
  if (!parent_span || !original_rows_span) {
    return base::ErrStatus("Failed to get tree spans from interpreter");
  }

  // Build tree columns and combine with data.
  auto final_count = static_cast<uint32_t>(parent_span->size());
  ASSIGN_OR_RETURN(auto tree_cols,
                   BuildTreeColumns(parent_span->b, final_count, pool_));
  return dataframe::Dataframe::HorizontalConcat(
      std::move(tree_cols),
      std::move(df_).SelectRows(original_rows_span->b, final_count));
}

// =============================================================================
// Private methods
// =============================================================================

void TreeTransformer::InitializeTreeStructure(uint32_t row_count) {
  using MakeC2P = interpreter::MakeChildToParentTreeStructure;

  // Allocate persistent scratch for parent and original_rows spans.
  auto parent_scratch = builder_->AllocateScratch(row_count);
  auto orig_scratch = builder_->AllocateScratch(row_count);

  parent_span_ = parent_scratch.span;
  original_rows_span_ = orig_scratch.span;

  // Allocate register for parent storage pointer.
  parent_storage_reg_ = builder_->AllocateRegister<interpreter::StoragePtr>();

  // Emit bytecode to initialize child-to-parent structure from parent storage.
  auto& op = builder_->AddOpcode<MakeC2P>(interpreter::Index<MakeC2P>());
  op.arg<MakeC2P::parent_id_storage_register>() = parent_storage_reg_;
  op.arg<MakeC2P::row_count>() = row_count;
  op.arg<MakeC2P::parent_span_register>() = parent_span_;
  op.arg<MakeC2P::original_rows_span_register>() = original_rows_span_;

  // Allocate all scratch buffers once for reuse across FilterTree() calls.
  // This avoids emitting AllocateIndices bytecode for each filter operation.
  filter_scratch_ = FilterScratch{
      builder_->AllocateScratch(row_count * 2),
      builder_->AllocateScratch(row_count),
      builder_->AllocateScratch(row_count),
      builder_->AllocateScratch(row_count + 1),
      builder_->AllocateScratch(row_count),
      builder_->AllocateScratch(row_count),
  };
}

void TreeTransformer::EnsureParentToChildStructure() {
  if (!p2c_stale_) {
    return;
  }
  using MakeP2C = interpreter::MakeParentToChildTreeStructure;

  auto& op = builder_->AddOpcode<MakeP2C>(interpreter::Index<MakeP2C>());
  op.arg<MakeP2C::parent_span_register>() = parent_span_;
  op.arg<MakeP2C::scratch_register>() = filter_scratch_->p2c_scratch.span;
  op.arg<MakeP2C::offsets_register>() = filter_scratch_->p2c_offsets.span;
  op.arg<MakeP2C::children_register>() = filter_scratch_->p2c_children.span;
  op.arg<MakeP2C::roots_register>() = filter_scratch_->p2c_roots.span;
  p2c_stale_ = false;
}

base::StatusOr<interpreter::RwHandle<BitVector>>
TreeTransformer::BuildFilterBitvector(
    uint32_t row_count,
    std::vector<dataframe::FilterSpec>& specs) {
  using InitRange = interpreter::InitRange;
  using Iota = interpreter::Iota;
  using SpanToBv = interpreter::IndexSpanToBitvector;

  // Initialize range covering all rows.
  auto range_reg = builder_->AllocateRegister<Range>();
  {
    auto& op = builder_->AddOpcode<InitRange>(interpreter::Index<InitRange>());
    op.arg<InitRange::size>() = row_count;
    op.arg<InitRange::dest_register>() = range_reg;
  }

  // Generate filter bytecode using QueryPlanBuilder::Filter.
  ASSIGN_OR_RETURN(auto filter_result,
                   dataframe::QueryPlanBuilder::Filter(
                       *builder_, *cache_,
                       interpreter::RwHandle<Range>(range_reg), df_, specs));

  // Capture register init specs for later initialization in ToDataframe().
  for (const auto& init : filter_result.register_inits) {
    register_inits_.emplace_back(init);
  }

  // Convert filtered result to bitvector.
  auto bv_reg = builder_->AllocateRegister<BitVector>();
  if (auto* range_ptr =
          std::get_if<interpreter::RwHandle<Range>>(&filter_result.indices)) {
    // For Range, use Iota to expand to indices first, then convert to
    // bitvector.
    auto filter_scratch = builder_->AllocateScratch(row_count);
    {
      auto& op = builder_->AddOpcode<Iota>(interpreter::Index<Iota>());
      op.arg<Iota::source_register>() = *range_ptr;
      op.arg<Iota::update_register>() = filter_scratch.span;
    }
    {
      auto& op = builder_->AddOpcode<SpanToBv>(interpreter::Index<SpanToBv>());
      op.arg<SpanToBv::indices_register>() = filter_scratch.span;
      op.arg<SpanToBv::bitvector_size>() = row_count;
      op.arg<SpanToBv::dest_register>() = bv_reg;
    }
    builder_->ReleaseScratch(filter_scratch);
  } else {
    auto span =
        std::get<interpreter::RwHandle<Span<uint32_t>>>(filter_result.indices);
    auto& op = builder_->AddOpcode<SpanToBv>(interpreter::Index<SpanToBv>());
    op.arg<SpanToBv::indices_register>() = span;
    op.arg<SpanToBv::bitvector_size>() = row_count;
    op.arg<SpanToBv::dest_register>() = bv_reg;
  }

  return bv_reg;
}

void TreeTransformer::EmitFilterTreeBytecode(
    interpreter::RwHandle<BitVector> keep_bv) {
  using Filter = interpreter::FilterTree;

  auto& op = builder_->AddOpcode<Filter>(interpreter::Index<Filter>());
  op.arg<Filter::offsets_register>() = filter_scratch_->p2c_offsets.span;
  op.arg<Filter::children_register>() = filter_scratch_->p2c_children.span;
  op.arg<Filter::roots_register>() = filter_scratch_->p2c_roots.span;
  op.arg<Filter::keep_bitvector_register>() = keep_bv;
  op.arg<Filter::parent_span_register>() = parent_span_;
  op.arg<Filter::original_rows_span_register>() = original_rows_span_;
  op.arg<Filter::scratch1_register>() = filter_scratch_->scratch1.span;
  op.arg<Filter::scratch2_register>() = filter_scratch_->scratch2.span;
}

}  // namespace perfetto::trace_processor::core::tree
