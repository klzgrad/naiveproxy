/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/db/column/range_overlay.h"

#include <cstdint>
#include <memory>
#include <optional>
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

namespace {

void AddOffsetToTokenIndex(std::vector<Token>& tokens, uint32_t offset) {
  for (auto& token : tokens) {
    token.index += offset;
  }
}

}  // namespace

void RangeOverlay::Flatten(uint32_t* start,
                           const uint32_t* end,
                           uint32_t stride) {
  for (uint32_t* it = start; it < end; it += stride) {
    *it += range_->start;
  }
}

RangeOverlay::ChainImpl::ChainImpl(std::unique_ptr<DataLayerChain> inner,
                                   const Range* range)
    : inner_(std::move(inner)), range_(range) {
  PERFETTO_CHECK(range->end <= inner_->size());
}

SingleSearchResult RangeOverlay::ChainImpl::SingleSearch(FilterOp op,
                                                         SqlValue sql_val,
                                                         uint32_t i) const {
  PERFETTO_DCHECK(i < range_->size());
  return inner_->SingleSearch(op, sql_val, i + range_->start);
}

SearchValidationResult RangeOverlay::ChainImpl::ValidateSearchConstraints(
    FilterOp op,
    SqlValue sql_val) const {
  if (sql_val.is_null() && op != FilterOp::kIsNotNull &&
      op != FilterOp::kIsNull) {
    return SearchValidationResult::kNoData;
  }
  return inner_->ValidateSearchConstraints(op, sql_val);
}

RangeOrBitVector RangeOverlay::ChainImpl::SearchValidated(
    FilterOp op,
    SqlValue sql_val,
    Range search_range) const {
  PERFETTO_DCHECK(search_range.size() <= range_->size());
  PERFETTO_TP_TRACE(metatrace::Category::DB, "RangeOverlay::Search");

  Range inner_search_range(search_range.start + range_->start,
                           search_range.end + range_->start);
  auto inner_res = inner_->SearchValidated(op, sql_val, inner_search_range);
  if (inner_res.IsRange()) {
    Range inner_res_range = std::move(inner_res).TakeIfRange();
    if (inner_res_range.empty()) {
      return RangeOrBitVector(Range());
    }
    return RangeOrBitVector(Range(inner_res_range.start - range_->start,
                                  inner_res_range.end - range_->start));
  }

  BitVector inner_res_bv = std::move(inner_res).TakeIfBitVector();
  if (range_->start == 0 && inner_res_bv.size() == range_->end) {
    return RangeOrBitVector{std::move(inner_res_bv)};
  }

  PERFETTO_DCHECK(inner_res_bv.size() == inner_search_range.end);
  PERFETTO_DCHECK(inner_res_bv.CountSetBits(inner_search_range.start) == 0);

  BitVector::Builder builder(search_range.end, search_range.start);
  uint32_t cur_val = search_range.start;
  uint32_t front_elements = builder.BitsUntilWordBoundaryOrFull();
  for (uint32_t i = 0; i < front_elements; ++i, ++cur_val) {
    builder.Append(inner_res_bv.IsSet(cur_val + range_->start));
  }

  // Fast path: we compare as many groups of 64 elements as we can.
  // This should be very easy for the compiler to auto-vectorize.
  uint32_t fast_path_elements = builder.BitsInCompleteWordsUntilFull();
  for (uint32_t i = 0; i < fast_path_elements; i += BitVector::kBitsInWord) {
    uint64_t word = 0;
    // This part should be optimised by SIMD and is expected to be fast.
    for (uint32_t k = 0; k < BitVector::kBitsInWord; ++k, ++cur_val) {
      bool comp_result = inner_res_bv.IsSet(cur_val + range_->start);
      word |= static_cast<uint64_t>(comp_result) << k;
    }
    builder.AppendWord(word);
  }

  // Slow path: we compare <64 elements and append to fill the Builder.
  uint32_t back_elements = builder.BitsUntilFull();
  for (uint32_t i = 0; i < back_elements; ++i, ++cur_val) {
    builder.Append(inner_res_bv.IsSet(cur_val + range_->start));
  }
  return RangeOrBitVector(std::move(builder).Build());
}

void RangeOverlay::ChainImpl::IndexSearchValidated(FilterOp op,
                                                   SqlValue sql_val,
                                                   Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB, "RangeOverlay::IndexSearch");
  AddOffsetToTokenIndex(indices.tokens, range_->start);
  inner_->IndexSearchValidated(op, sql_val, indices);
}

void RangeOverlay::ChainImpl::StableSort(Token* start,
                                         Token* end,
                                         SortDirection direction) const {
  for (Token* it = start; it != end; ++it) {
    it->index += range_->start;
  }
  inner_->StableSort(start, end, direction);
}

void RangeOverlay::ChainImpl::Distinct(Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB, "RangeOverlay::Distinct");
  AddOffsetToTokenIndex(indices.tokens, range_->start);
  inner_->Distinct(indices);
}

std::optional<Token> RangeOverlay::ChainImpl::MaxElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB, "RangeOverlay::MaxElement");
  AddOffsetToTokenIndex(indices.tokens, range_->start);
  return inner_->MaxElement(indices);
}

std::optional<Token> RangeOverlay::ChainImpl::MinElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB, "RangeOverlay::MinElement");
  AddOffsetToTokenIndex(indices.tokens, range_->start);
  return inner_->MinElement(indices);
}

SqlValue RangeOverlay::ChainImpl::Get_AvoidUsingBecauseSlow(
    uint32_t index) const {
  return inner_->Get_AvoidUsingBecauseSlow(index + range_->start);
}

}  // namespace perfetto::trace_processor::column
