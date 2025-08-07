/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/db/column/arrangement_overlay.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/bit_vector.h"
#include "src/trace_processor/db/column/data_layer.h"
#include "src/trace_processor/db/column/types.h"
#include "src/trace_processor/tp_metatrace.h"

#include "protos/perfetto/trace_processor/metatrace_categories.pbzero.h"

namespace perfetto::trace_processor::column {

void ArrangementOverlay::Flatten(uint32_t* start,
                                 const uint32_t* end,
                                 uint32_t stride) {
  for (uint32_t* it = start; it < end; it += stride) {
    *it = (*arrangement_)[*it];
  }
}

ArrangementOverlay::ChainImpl::ChainImpl(
    std::unique_ptr<DataLayerChain> inner,
    const std::vector<uint32_t>* arrangement,
    Indices::State arrangement_state,
    bool does_arrangement_order_storage)
    : inner_(std::move(inner)),
      arrangement_(arrangement),
      arrangement_state_(arrangement_state),
      does_arrangement_order_storage_(does_arrangement_order_storage) {}

SingleSearchResult ArrangementOverlay::ChainImpl::SingleSearch(
    FilterOp op,
    SqlValue sql_val,
    uint32_t index) const {
  return inner_->SingleSearch(op, sql_val, (*arrangement_)[index]);
}

SearchValidationResult ArrangementOverlay::ChainImpl::ValidateSearchConstraints(
    FilterOp op,
    SqlValue value) const {
  return inner_->ValidateSearchConstraints(op, value);
}

RangeOrBitVector ArrangementOverlay::ChainImpl::SearchValidated(
    FilterOp op,
    SqlValue sql_val,
    Range in) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "ArrangementOverlay::ChainImpl::Search");

  if (does_arrangement_order_storage_ && op != FilterOp::kGlob &&
      op != FilterOp::kRegex) {
    OrderedIndices indices{arrangement_->data() + in.start, in.size(),
                           arrangement_state_};
    if (op == FilterOp::kNe) {
      // Do an equality search and "invert" the range.
      Range inner_res =
          inner_->OrderedIndexSearchValidated(FilterOp::kEq, sql_val, indices);
      BitVector bv(in.start);
      bv.Resize(in.start + inner_res.start, true);
      bv.Resize(in.start + inner_res.end, false);
      bv.Resize(in.end, true);
      return RangeOrBitVector(std::move(bv));
    }
    Range inner_res = inner_->OrderedIndexSearchValidated(op, sql_val, indices);
    return RangeOrBitVector(
        Range(in.start + inner_res.start, in.start + inner_res.end));
  }

  const auto& arrangement = *arrangement_;
  PERFETTO_DCHECK(in.end <= arrangement.size());
  const auto [min_i, max_i] =
      std::minmax_element(arrangement.begin() + static_cast<int32_t>(in.start),
                          arrangement.begin() + static_cast<int32_t>(in.end));

  auto storage_result =
      inner_->SearchValidated(op, sql_val, Range(*min_i, *max_i + 1));
  BitVector::Builder builder(in.end, in.start);
  if (storage_result.IsRange()) {
    Range storage_range = std::move(storage_result).TakeIfRange();
    for (uint32_t i = in.start; i < in.end; ++i) {
      builder.Append(storage_range.Contains(arrangement[i]));
    }
  } else {
    BitVector storage_bitvector = std::move(storage_result).TakeIfBitVector();
    PERFETTO_DCHECK(storage_bitvector.size() == *max_i + 1);

    // After benchmarking, it turns out this complexity *is* actually worthwhile
    // and has a noticeable impact on the performance of this function in real
    // world tables.

    // Fast path: we compare as many groups of 64 elements as we can.
    // This should be very easy for the compiler to auto-vectorize.
    const uint32_t* arrangement_idx = arrangement.data() + in.start;
    uint32_t fast_path_elements = builder.BitsInCompleteWordsUntilFull();
    for (uint32_t i = 0; i < fast_path_elements; i += BitVector::kBitsInWord) {
      uint64_t word = 0;
      // This part should be optimised by SIMD and is expected to be fast.
      for (uint32_t k = 0; k < BitVector::kBitsInWord; ++k, ++arrangement_idx) {
        bool comp_result = storage_bitvector.IsSet(*arrangement_idx);
        word |= static_cast<uint64_t>(comp_result) << k;
      }
      builder.AppendWord(word);
    }

    // Slow path: we compare <64 elements and append to fill the Builder.
    uint32_t back_elements = builder.BitsUntilFull();
    for (uint32_t i = 0; i < back_elements; ++i, ++arrangement_idx) {
      builder.Append(storage_bitvector.IsSet(*arrangement_idx));
    }
  }
  return RangeOrBitVector(std::move(builder).Build());
}

void ArrangementOverlay::ChainImpl::IndexSearchValidated(
    FilterOp op,
    SqlValue sql_val,
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "ArrangementOverlay::ChainImpl::IndexSearch");

  for (auto& i : indices.tokens) {
    i.index = (*arrangement_)[i.index];
  }
  // If the indices state is monotonic, we can just pass the arrangement's
  // state.
  indices.state = indices.state == Indices::State::kMonotonic
                      ? arrangement_state_
                      : Indices::State::kNonmonotonic;
  return inner_->IndexSearchValidated(op, sql_val, indices);
}

void ArrangementOverlay::ChainImpl::StableSort(Token* start,
                                               Token* end,
                                               SortDirection direction) const {
  for (Token* it = start; it != end; ++it) {
    it->index = (*arrangement_)[it->index];
  }
  inner_->StableSort(start, end, direction);
}

void ArrangementOverlay::ChainImpl::Distinct(Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "ArrangementOverlay::ChainImpl::Distinct");
  // TODO(mayzner): Utilize `does_arrangmeent_order_storage_`.
  std::unordered_set<uint32_t> s;
  indices.tokens.erase(
      std::remove_if(indices.tokens.begin(), indices.tokens.end(),
                     [this, &s](Token& idx) {
                       if (s.insert(idx.index).second) {
                         idx.index = (*arrangement_)[idx.index];
                         return false;
                       }
                       return true;
                     }),
      indices.tokens.end());
  inner_->Distinct(indices);
}

std::optional<Token> ArrangementOverlay::ChainImpl::MaxElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "ArrangementOverlay::ChainImpl::MaxElement");
  for (auto& i : indices.tokens) {
    i.index = (*arrangement_)[i.index];
  }
  // If the indices state is monotonic, we can just pass the arrangement's
  // state.
  indices.state = indices.state == Indices::State::kMonotonic
                      ? arrangement_state_
                      : Indices::State::kNonmonotonic;
  return inner_->MaxElement(indices);
}

std::optional<Token> ArrangementOverlay::ChainImpl::MinElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "ArrangementOverlay::ChainImpl::MinElement");
  for (auto& i : indices.tokens) {
    i.index = (*arrangement_)[i.index];
  }
  // If the indices state is monotonic, we can just pass the arrangement's
  // state.
  indices.state = indices.state == Indices::State::kMonotonic
                      ? arrangement_state_
                      : Indices::State::kNonmonotonic;
  return inner_->MinElement(indices);
}

SqlValue ArrangementOverlay::ChainImpl::Get_AvoidUsingBecauseSlow(
    uint32_t index) const {
  return inner_->Get_AvoidUsingBecauseSlow((*arrangement_)[index]);
}

}  // namespace perfetto::trace_processor::column
