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

#include "src/trace_processor/db/column/selector_overlay.h"

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

constexpr uint32_t kIndexOfNthSetRatio = 32;

void TranslateToInnerIndices(const BitVector& selector,
                             std::vector<Token>& tokens) {
  if (selector.size() == selector.CountSetBits()) {
    return;
  }
  if (tokens.size() < selector.size() / kIndexOfNthSetRatio) {
    for (auto& token : tokens) {
      token.index = selector.IndexOfNthSet(token.index);
    }
    return;
  }
  // TODO(mayzner): once we have a reverse index for IndexOfNthSet in
  // BitVector, this should no longer be necessary.
  std::vector<uint32_t> lookup = selector.GetSetBitIndices();
  for (auto& token : tokens) {
    token.index = lookup[token.index];
  }
}

void TranslateToInnerIndices(const BitVector& selector,
                             uint32_t* start,
                             const uint32_t* end,
                             uint32_t stride) {
  if (selector.size() == selector.CountSetBits()) {
    return;
  }
  auto size = static_cast<uint32_t>(end - start);
  if (size < selector.size() / kIndexOfNthSetRatio) {
    for (uint32_t* it = start; it < end; it += stride) {
      *it = selector.IndexOfNthSet(*it);
    }
    return;
  }
  // TODO(mayzner): once we have a reverse index for IndexOfNthSet in
  // BitVector, this should no longer be necessary.
  std::vector<uint32_t> lookup = selector.GetSetBitIndices();
  for (uint32_t* it = start; it < end; it += stride) {
    *it = lookup[*it];
  }
}

}  // namespace

void SelectorOverlay::Flatten(uint32_t* start,
                              const uint32_t* end,
                              uint32_t stride) {
  TranslateToInnerIndices(*selector_, start, end, stride);
}

SelectorOverlay::ChainImpl::ChainImpl(std::unique_ptr<DataLayerChain> inner,
                                      const BitVector* selector)
    : inner_(std::move(inner)), selector_(selector) {}

SingleSearchResult SelectorOverlay::ChainImpl::SingleSearch(FilterOp op,
                                                            SqlValue sql_val,
                                                            uint32_t i) const {
  return inner_->SingleSearch(op, sql_val, selector_->IndexOfNthSet(i));
}

SearchValidationResult SelectorOverlay::ChainImpl::ValidateSearchConstraints(
    FilterOp op,
    SqlValue sql_val) const {
  if (sql_val.is_null() && op != FilterOp::kIsNotNull &&
      op != FilterOp::kIsNull) {
    return SearchValidationResult::kNoData;
  }
  return inner_->ValidateSearchConstraints(op, sql_val);
}

RangeOrBitVector SelectorOverlay::ChainImpl::SearchValidated(FilterOp op,
                                                             SqlValue sql_val,
                                                             Range in) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "SelectorOverlay::ChainImpl::Search");

  // Figure out the bounds of the indicess in the underlying storage and
  // search it.
  uint32_t start_idx = selector_->IndexOfNthSet(in.start);
  uint32_t end_idx = selector_->IndexOfNthSet(in.end - 1) + 1;

  auto storage_result =
      inner_->SearchValidated(op, sql_val, Range(start_idx, end_idx));
  if (storage_result.IsRange()) {
    Range storage_range = std::move(storage_result).TakeIfRange();
    if (storage_range.empty()) {
      return RangeOrBitVector(Range());
    }
    uint32_t out_start = selector_->CountSetBits(storage_range.start);
    uint32_t out_end = selector_->CountSetBits(storage_range.end);
    return RangeOrBitVector(Range(out_start, out_end));
  }

  BitVector storage_bitvector = std::move(storage_result).TakeIfBitVector();
  PERFETTO_DCHECK(storage_bitvector.size() <= selector_->size());
  storage_bitvector.SelectBits(*selector_);
  if (storage_bitvector.size() == 0) {
    return RangeOrBitVector(std::move(storage_bitvector));
  }
  PERFETTO_DCHECK(storage_bitvector.size() == in.end);
  return RangeOrBitVector(std::move(storage_bitvector));
}

void SelectorOverlay::ChainImpl::IndexSearchValidated(FilterOp op,
                                                      SqlValue sql_val,
                                                      Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "SelectorOverlay::ChainImpl::IndexSearch");
  TranslateToInnerIndices(*selector_, indices.tokens);
  return inner_->IndexSearchValidated(op, sql_val, indices);
}

void SelectorOverlay::ChainImpl::StableSort(Token* start,
                                            Token* end,
                                            SortDirection direction) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "SelectorOverlay::ChainImpl::StableSort");
  for (Token* it = start; it != end; ++it) {
    it->index = selector_->IndexOfNthSet(it->index);
  }
  inner_->StableSort(start, end, direction);
}

void SelectorOverlay::ChainImpl::Distinct(Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "SelectorOverlay::ChainImpl::Distinct");
  TranslateToInnerIndices(*selector_, indices.tokens);
  return inner_->Distinct(indices);
}

std::optional<Token> SelectorOverlay::ChainImpl::MaxElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "SelectorOverlay::ChainImpl::MaxElement");
  TranslateToInnerIndices(*selector_, indices.tokens);
  return inner_->MaxElement(indices);
}

std::optional<Token> SelectorOverlay::ChainImpl::MinElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "SelectorOverlay::ChainImpl::MinElement");
  TranslateToInnerIndices(*selector_, indices.tokens);
  return inner_->MinElement(indices);
}

SqlValue SelectorOverlay::ChainImpl::Get_AvoidUsingBecauseSlow(
    uint32_t index) const {
  return inner_->Get_AvoidUsingBecauseSlow(selector_->IndexOfNthSet(index));
}

}  // namespace perfetto::trace_processor::column
