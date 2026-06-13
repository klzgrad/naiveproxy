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

#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/common/op_types.h"
#include "src/trace_processor/core/common/storage_types.h"
#include "src/trace_processor/core/common/tree_types.h"
#include "src/trace_processor/core/common/value_fetcher.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/interpreter/bytecode_builder.h"
#include "src/trace_processor/core/interpreter/bytecode_instructions.h"
#include "src/trace_processor/core/interpreter/bytecode_interpreter.h"
#include "src/trace_processor/core/interpreter/bytecode_interpreter_impl.h"  // IWYU pragma: keep
#include "src/trace_processor/core/interpreter/bytecode_registers.h"
#include "src/trace_processor/core/interpreter/interpreter_types.h"
#include "src/trace_processor/core/tree/propagate_spec.h"
#include "src/trace_processor/core/tree/tree_columns.h"
#include "src/trace_processor/core/util/bit_vector.h"
#include "src/trace_processor/core/util/range.h"
#include "src/trace_processor/core/util/slab.h"
#include "src/trace_processor/core/util/span.h"

namespace perfetto::trace_processor::core::tree {
namespace {

namespace i = interpreter;

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

// Pushes one value from raw column data to the builder.
void PushColumnValue(dataframe::AdhocDataframeBuilder& builder,
                     uint32_t col_idx,
                     StorageType type,
                     const uint8_t* data,
                     uint32_t row) {
  switch (type.index()) {
    case StorageType::GetTypeIndex<Uint32>():
      builder.PushNonNull(col_idx,
                          reinterpret_cast<const uint32_t*>(data)[row]);
      break;
    case StorageType::GetTypeIndex<Int32>():
      builder.PushNonNull(
          col_idx,
          static_cast<int64_t>(reinterpret_cast<const int32_t*>(data)[row]));
      break;
    case StorageType::GetTypeIndex<Int64>():
      builder.PushNonNull(col_idx, reinterpret_cast<const int64_t*>(data)[row]);
      break;
    case StorageType::GetTypeIndex<Double>():
      builder.PushNonNull(col_idx, reinterpret_cast<const double*>(data)[row]);
      break;
    case StorageType::GetTypeIndex<String>():
      builder.PushNonNull(col_idx,
                          reinterpret_cast<const StringPool::Id*>(data)[row]);
      break;
    default:
      PERFETTO_FATAL("Unsupported storage type");
  }
}

// Builds the data columns portion of the output dataframe.
// |get_col_source| is called with column index and must return a pair of
// (data pointer, null bitvector reference).
template <typename F>
base::StatusOr<dataframe::Dataframe> BuildDataColumns(
    const std::vector<std::string>& names,
    const std::vector<TreeColumns::Column>& col_defs,
    uint32_t row_count,
    StringPool* pool,
    F get_col_source) {
  auto builder = dataframe::AdhocDataframeBuilder(
      names, pool,
      dataframe::AdhocDataframeBuilder::Options{
          {},
          dataframe::NullabilityType::kDenseNull,
      });
  for (uint32_t ci = 0; ci < names.size(); ++ci) {
    auto [data, null_bv] = get_col_source(ci);
    bool has_null = null_bv.size() > 0;
    for (uint32_t row = 0; row < row_count; ++row) {
      if (has_null && !null_bv.is_set(row)) {
        builder.PushNull(ci);
      } else {
        PushColumnValue(builder, ci, col_defs[ci].type, data, row);
      }
    }
  }
  return std::move(builder).Build();
}

}  // namespace

TreeTransformer::TreeTransformer(TreeColumns cols, StringPool* pool)
    : cols_(std::move(cols)),
      pool_(pool),
      builder_(std::make_unique<i::BytecodeBuilder>()) {
  uint32_t n = cols_.row_count;
  if (n == 0) {
    return;
  }

  auto range_reg = builder_->AllocateRegister<Range>();
  {
    using B = i::InitRange;
    auto& op = builder_->AddOpcode<B>(i::Index<B>());
    op.arg<B::size>() = n;
    op.arg<B::dest_register>() = range_reg;
  }
  auto slab_reg = builder_->AllocateRegister<Slab<uint32_t>>();
  auto span_reg = builder_->AllocateRegister<Span<uint32_t>>();
  span_reg_index_ = span_reg.index;
  {
    using B = i::AllocateIndices;
    auto& op = builder_->AddOpcode<B>(i::Index<B>());
    op.arg<B::size>() = n;
    op.arg<B::dest_slab_register>() = slab_reg;
    op.arg<B::dest_span_register>() = span_reg;
  }
  {
    using B = i::Iota;
    auto& op = builder_->AddOpcode<B>(i::Index<B>());
    op.arg<B::source_register>() = range_reg;
    op.arg<B::update_register>() = span_reg;
  }

  auto ts_reg = builder_->AllocateRegister<std::unique_ptr<i::TreeState>>();
  tree_state_reg_index_ = ts_reg.index;
}

TreeTransformer::~TreeTransformer() = default;
TreeTransformer::TreeTransformer(TreeTransformer&&) noexcept = default;
TreeTransformer& TreeTransformer::operator=(TreeTransformer&&) noexcept =
    default;

std::optional<uint32_t> TreeTransformer::ResolveColumn(
    std::string_view name) const {
  for (uint32_t i = 0; i < cols_.names.size(); ++i) {
    if (cols_.names[i] == name) {
      return i;
    }
  }
  return std::nullopt;
}

base::Status TreeTransformer::FilterTree(
    std::vector<dataframe::FilterSpec> specs,
    std::vector<SqlValue> values) {
  if (cols_.row_count == 0 || specs.empty()) {
    return base::OkStatus();
  }

  has_operations_ = true;
  i::RwHandle<Span<uint32_t>> span_reg(span_reg_index_);

  for (size_t si = 0; si < specs.size(); ++si) {
    auto& spec = specs[si];
    const auto& col = cols_.columns[spec.col];
    StorageType ct = col.type;
    const BitVector* null_bv = col.null_bv.size() > 0 ? &col.null_bv : nullptr;

    // Handle null ops (IsNull/IsNotNull).
    if (auto null_op = spec.op.TryDowncast<i::NullOp>()) {
      spec.value_index = filter_value_count_++;
      filter_values_.push_back(values[si]);
      if (null_bv) {
        auto reg = builder_->AllocateRegister<i::NullBitvector>();
        using B = i::NullFilterBase;
        auto& bc = builder_->AddOpcode<B>(i::Index<i::NullFilter>(*null_op));
        bc.arg<B::null_bv_register>() = reg;
        bc.arg<B::update_register>() = span_reg;
        reg_inits_.push_back({RegInit::kNullBv, reg.index, spec.col});
      }
      continue;
    }

    // In is not supported for tree filters.
    PERFETTO_CHECK(!spec.op.Is<In>());

    auto non_null_op = spec.op.TryDowncast<i::NonNullOp>();
    PERFETTO_CHECK(non_null_op);

    // Cast filter value.
    auto value_reg = builder_->AllocateRegister<i::CastFilterValueResult>();
    {
      using B = i::CastFilterValueBase;
      auto& bc = builder_->AddOpcode<B>(i::Index<i::CastFilterValue>(ct));
      bc.arg<B::fval_handle>() = {filter_value_count_};
      bc.arg<B::write_register>() = value_reg;
      bc.arg<B::op>() = *non_null_op;
      spec.value_index = filter_value_count_++;
      filter_values_.push_back(values[si]);
    }

    // Prune null indices if column has nulls.
    if (null_bv) {
      auto reg = builder_->AllocateRegister<i::NullBitvector>();
      using B = i::NullFilter<IsNotNull>;
      auto& bc = builder_->AddOpcode<B>(i::Index<B>());
      bc.arg<B::null_bv_register>() = reg;
      bc.arg<B::update_register>() = span_reg;
      reg_inits_.push_back({RegInit::kNullBv, reg.index, spec.col});
    }

    // Allocate storage register and emit filter bytecode.
    auto storage_reg = builder_->AllocateRegister<i::StoragePtr>();
    reg_inits_.push_back({RegInit::kStorage, storage_reg.index, spec.col});

    if (ct.Is<String>()) {
      auto op = non_null_op->TryDowncast<i::StringOp>();
      PERFETTO_CHECK(op);
      using B = i::StringFilterBase;
      auto& bc = builder_->AddOpcode<B>(i::Index<i::StringFilter>(*op));
      bc.arg<B::storage_register>() = storage_reg;
      bc.arg<B::val_register>() = value_reg;
      bc.arg<B::source_register>() = span_reg;
      bc.arg<B::update_register>() = span_reg;
    } else {
      auto nst = ct.TryDowncast<i::NonStringType>();
      PERFETTO_CHECK(nst);
      auto op = non_null_op->TryDowncast<i::NonStringOp>();
      PERFETTO_CHECK(op);
      using B = i::NonStringFilterBase;
      auto& bc =
          builder_->AddOpcode<B>(i::Index<i::NonStringFilter>(*nst, *op));
      bc.arg<B::storage_register>() = storage_reg;
      bc.arg<B::val_register>() = value_reg;
      bc.arg<B::source_register>() = span_reg;
      bc.arg<B::update_register>() = span_reg;
    }
  }

  // Emit FilterTreeState.
  i::RwHandle<std::unique_ptr<i::TreeState>> ts_reg(tree_state_reg_index_);
  {
    using F = i::FilterTreeState;
    auto& op = builder_->AddOpcode<F>(i::Index<F>());
    op.arg<F::tree_state_register>() = ts_reg;
    op.arg<F::indices_register>() = span_reg;
  }

  return base::OkStatus();
}

// Returns the size in bytes of one element for the given storage type.
static uint32_t ElementSize(StorageType type) {
  switch (type.index()) {
    case StorageType::GetTypeIndex<Uint32>():
      return sizeof(uint32_t);
    case StorageType::GetTypeIndex<Int32>():
      return sizeof(int32_t);
    case StorageType::GetTypeIndex<Int64>():
      return sizeof(int64_t);
    case StorageType::GetTypeIndex<Double>():
      return sizeof(double);
    case StorageType::GetTypeIndex<String>():
      return sizeof(StringPool::Id);
    default:
      PERFETTO_FATAL("Unsupported storage type");
  }
}

base::Status TreeTransformer::PropagateDown(std::vector<PropagateSpec> specs) {
  if (cols_.row_count == 0 || specs.empty()) {
    return base::OkStatus();
  }

  uint32_t spec_start = static_cast<uint32_t>(propagate_specs_.size());

  for (auto& spec : specs) {
    auto src = ResolveColumn(spec.source_col_name);
    if (!src) {
      return base::ErrStatus("propagate_down: source column '%s' not found",
                             spec.source_col_name.c_str());
    }
    StorageType st = cols_.columns[*src].type;
    if (st.Is<String>()) {
      return base::ErrStatus(
          "propagate_down: string columns are not supported (column '%s')",
          spec.source_col_name.c_str());
    }
    if (st.Is<Id>()) {
      return base::ErrStatus(
          "propagate_down: Id columns are not supported (column '%s')",
          spec.source_col_name.c_str());
    }

    // Add a new column.
    uint32_t dest = static_cast<uint32_t>(cols_.names.size());
    cols_.names.push_back(std::move(spec.output_col_name));

    TreeColumns::Column oc;
    oc.type = st;
    oc.elem_size = ElementSize(st);
    oc.data = Slab<uint8_t>::Alloc(static_cast<uint64_t>(cols_.row_count) *
                                   oc.elem_size);
    cols_.columns.push_back(std::move(oc));

    propagate_specs_.push_back({*src, dest, spec.agg_op});
  }

  // Emit PropagateTreeDown bytecode with the spec range.
  has_operations_ = true;
  i::RwHandle<std::unique_ptr<i::TreeState>> ts_reg(tree_state_reg_index_);
  {
    using P = i::PropagateTreeDown;
    auto& op = builder_->AddOpcode<P>(i::Index<P>());
    op.arg<P::tree_state_register>() = ts_reg;
    op.arg<P::spec_start>() = spec_start;
    op.arg<P::spec_count>() =
        static_cast<uint32_t>(propagate_specs_.size()) - spec_start;
  }

  return base::OkStatus();
}

base::StatusOr<dataframe::Dataframe> TreeTransformer::ToDataframe() && {
  uint32_t n = cols_.row_count;

  // No-op case: build output directly from owned data.
  if (n == 0 || !has_operations_) {
    auto tree_builder = dataframe::AdhocDataframeBuilder(
        {"_tree_id", "_tree_parent_id"}, pool_,
        dataframe::AdhocDataframeBuilder::Options{
            {},
            dataframe::NullabilityType::kDenseNull,
        });
    for (uint32_t i = 0; i < n; ++i) {
      tree_builder.PushNonNull(0, i);
      if (cols_.parent[i] == kNullParent) {
        tree_builder.PushNull(1);
      } else {
        tree_builder.PushNonNull(1, cols_.parent[i]);
      }
    }
    ASSIGN_OR_RETURN(auto tree_cols, std::move(tree_builder).Build());

    ASSIGN_OR_RETURN(
        auto data_cols,
        BuildDataColumns(
            cols_.names, cols_.columns, n, pool_,
            [&](uint32_t ci) -> std::pair<const uint8_t*, const BitVector&> {
              return {cols_.columns[ci].data.begin(),
                      cols_.columns[ci].null_bv};
            }));
    return dataframe::Dataframe::HorizontalConcat(std::move(tree_cols),
                                                  std::move(data_cols));
  }

  // Build TreeState by moving owned data into it.
  auto ts = std::make_unique<i::TreeState>();
  ts->row_count = n;
  ts->parent = std::move(cols_.parent);

  ts->original_rows = Slab<uint32_t>::Alloc(n);
  std::iota(ts->original_rows.begin(), ts->original_rows.begin() + n, 0u);

  for (auto& col : cols_.columns) {
    i::TreeState::ColumnStorage cs;
    cs.data = std::move(col.data);
    cs.elem_size = col.elem_size;
    ts->columns.push_back(std::move(cs));
    ts->null_bitvectors.push_back(std::move(col.null_bv));
  }

  // Populate propagate_down_specs (column indices map 1:1).
  for (const auto& pi : propagate_specs_) {
    using PDS = interpreter::TreeState::PropagateDownSpec;
    ts->propagate_down_specs.push_back(PDS{
        pi.agg_op,
        pi.source_col,
        pi.dest_col,
        cols_.columns[pi.dest_col].type,
    });
  }

  ts->p2c_offsets = Slab<uint32_t>::Alloc(n + 1);
  ts->p2c_children = Slab<uint32_t>::Alloc(n);
  ts->p2c_roots = Slab<uint32_t>::Alloc(n);
  ts->p2c_valid = false;
  ts->scratch1 = Slab<uint32_t>::Alloc(static_cast<uint64_t>(n) * 2);
  ts->scratch2 = Slab<uint32_t>::Alloc(n);
  ts->keep_bv = BitVector::CreateWithSize(n, false);

  // Initialize interpreter.
  i::Interpreter<TreeValueFetcher> interp;
  interp.Initialize(builder_->bytecode(), builder_->register_count(), pool_);
  interp.SetRegisterValue(
      i::WriteHandle<std::unique_ptr<i::TreeState>>(tree_state_reg_index_),
      std::move(ts));

  const auto* ts_ptr = interp.GetRegisterValue(
      i::ReadHandle<std::unique_ptr<i::TreeState>>(tree_state_reg_index_));
  PERFETTO_CHECK(ts_ptr && *ts_ptr);
  const auto& ts_ref = **ts_ptr;

  for (const auto& ri : reg_inits_) {
    switch (ri.kind) {
      case RegInit::kStorage:
        interp.SetRegisterValue(i::WriteHandle<i::StoragePtr>(ri.reg),
                                i::StoragePtr{
                                    ts_ref.columns[ri.col].data.begin(),
                                    cols_.columns[ri.col].type,
                                });
        break;
      case RegInit::kNullBv:
        interp.SetRegisterValue(
            i::WriteHandle<i::NullBitvector>(ri.reg),
            i::NullBitvector{&ts_ref.null_bitvectors[ri.col], {}});
        break;
    }
  }

  TreeValueFetcher fetcher(filter_values_.data());
  interp.Execute(fetcher);

  // Build output from final TreeState.
  const auto& final_ts = **ts_ptr;
  uint32_t final_count = final_ts.row_count;

  auto tree_builder = dataframe::AdhocDataframeBuilder(
      {"_tree_id", "_tree_parent_id"}, pool_,
      dataframe::AdhocDataframeBuilder::Options{
          {},
          dataframe::NullabilityType::kDenseNull,
      });
  for (uint32_t i = 0; i < final_count; ++i) {
    tree_builder.PushNonNull(0, i);
    if (final_ts.parent[i] == kNullParent) {
      tree_builder.PushNull(1);
    } else {
      tree_builder.PushNonNull(1, final_ts.parent[i]);
    }
  }
  ASSIGN_OR_RETURN(auto tree_cols, std::move(tree_builder).Build());

  ASSIGN_OR_RETURN(
      auto data_cols,
      BuildDataColumns(
          cols_.names, cols_.columns, final_count, pool_,
          [&](uint32_t ci) -> std::pair<const uint8_t*, const BitVector&> {
            return {final_ts.columns[ci].data.begin(),
                    final_ts.null_bitvectors[ci]};
          }));
  return dataframe::Dataframe::HorizontalConcat(std::move(tree_cols),
                                                std::move(data_cols));
}

}  // namespace perfetto::trace_processor::core::tree
