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

#include "src/trace_processor/db/column/null_overlay.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/bit_vector.h"
#include "src/trace_processor/db/column/data_layer.h"
#include "src/trace_processor/db/column/types.h"
#include "src/trace_processor/tp_metatrace.h"

#include "protos/perfetto/trace_processor/metatrace_categories.pbzero.h"

namespace perfetto::trace_processor::column {
namespace {

namespace {
using Indices = DataLayerChain::Indices;

std::optional<Token> UpdateIndicesForInner(Indices& indices,
                                           const BitVector& non_null) {
  // Find first NULL.
  auto first_null_it = std::find_if(
      indices.tokens.begin(), indices.tokens.end(),
      [&non_null](const Token& t) { return !non_null.IsSet(t.index); });

  // Save first NULL.
  std::optional<Token> null_tok;
  if (first_null_it != indices.tokens.end()) {
    null_tok = *first_null_it;
  }

  // Erase all NULLs.
  indices.tokens.erase(std::remove_if(first_null_it, indices.tokens.end(),
                                      [&non_null](const Token& idx) {
                                        return !non_null.IsSet(idx.index);
                                      }),
                       indices.tokens.end());

  // Update index of each token so they all point to inner.
  for (auto& token : indices.tokens) {
    token.index = non_null.CountSetBits(token.index);
  }
  return null_tok;
}
}  // namespace

BitVector ReconcileStorageResult(FilterOp op,
                                 const BitVector& non_null,
                                 RangeOrBitVector storage_result,
                                 Range in_range) {
  PERFETTO_CHECK(in_range.end <= non_null.size());

  // Reconcile the results of the Search operation with the non-null indices
  // to ensure only those positions are set.
  BitVector res;
  if (storage_result.IsRange()) {
    Range range = std::move(storage_result).TakeIfRange();
    if (!range.empty()) {
      res = non_null.IntersectRange(non_null.IndexOfNthSet(range.start),
                                    non_null.IndexOfNthSet(range.end - 1) + 1);

      // We should always have at least as many elements as the input range
      // itself.
      PERFETTO_CHECK(res.size() <= in_range.end);
    }
  } else {
    res = non_null.Copy();
    res.UpdateSetBits(std::move(storage_result).TakeIfBitVector());
  }

  // Ensure that |res| exactly matches the size which we need to return,
  // padding with zeros or truncating if necessary.
  res.Resize(in_range.end, false);

  // For the IS NULL constraint, we also need to include all the null indices
  // themselves.
  if (PERFETTO_UNLIKELY(op == FilterOp::kIsNull)) {
    BitVector null = non_null.IntersectRange(in_range.start, in_range.end);
    null.Resize(in_range.end, false);
    null.Not();
    res.Or(null);
  }
  return res;
}

}  // namespace

void NullOverlay::Flatten(uint32_t* start,
                          const uint32_t* end,
                          uint32_t stride) {
  for (uint32_t* it = start; it < end; it += stride) {
    if (non_null_->IsSet(*it)) {
      *it = non_null_->CountSetBits(*it);
    } else {
      *it = std::numeric_limits<uint32_t>::max();
    }
  }
}

SingleSearchResult NullOverlay::ChainImpl::SingleSearch(FilterOp op,
                                                        SqlValue sql_val,
                                                        uint32_t index) const {
  switch (op) {
    case FilterOp::kIsNull:
      return non_null_->IsSet(index)
                 ? inner_->SingleSearch(op, sql_val,
                                        non_null_->CountSetBits(index))
                 : SingleSearchResult::kMatch;
    case FilterOp::kIsNotNull:
    case FilterOp::kEq:
    case FilterOp::kGe:
    case FilterOp::kGt:
    case FilterOp::kLt:
    case FilterOp::kLe:
    case FilterOp::kNe:
    case FilterOp::kGlob:
    case FilterOp::kRegex:
      return non_null_->IsSet(index)
                 ? inner_->SingleSearch(op, sql_val,
                                        non_null_->CountSetBits(index))
                 : SingleSearchResult::kNoMatch;
  }
  PERFETTO_FATAL("For GCC");
}

NullOverlay::ChainImpl::ChainImpl(std::unique_ptr<DataLayerChain> inner,
                                  const BitVector* non_null)
    : inner_(std::move(inner)), non_null_(non_null) {
  PERFETTO_DCHECK(non_null_->CountSetBits() <= inner_->size());
}

SearchValidationResult NullOverlay::ChainImpl::ValidateSearchConstraints(
    FilterOp op,
    SqlValue sql_val) const {
  if (op == FilterOp::kIsNull || op == FilterOp::kIsNotNull) {
    return SearchValidationResult::kOk;
  }
  if (sql_val.is_null()) {
    return SearchValidationResult::kNoData;
  }
  return inner_->ValidateSearchConstraints(op, sql_val);
}

RangeOrBitVector NullOverlay::ChainImpl::SearchValidated(FilterOp op,
                                                         SqlValue sql_val,
                                                         Range in) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB, "NullOverlay::ChainImpl::Search");

  if (op == FilterOp::kIsNull) {
    switch (inner_->ValidateSearchConstraints(op, sql_val)) {
      case SearchValidationResult::kNoData: {
        // There is no need to search in underlying storage. It's enough to
        // intersect the |non_null_|.
        BitVector res = non_null_->Copy();
        res.Resize(in.end, false);
        res.Not();
        return RangeOrBitVector(res.IntersectRange(in.start, in.end));
      }
      case SearchValidationResult::kAllData:
        return RangeOrBitVector(in);
      case SearchValidationResult::kOk:
        break;
    }
  } else if (op == FilterOp::kIsNotNull) {
    switch (inner_->ValidateSearchConstraints(op, sql_val)) {
      case SearchValidationResult::kNoData:
        return RangeOrBitVector(Range());
      case SearchValidationResult::kAllData:
        return RangeOrBitVector(non_null_->IntersectRange(in.start, in.end));
      case SearchValidationResult::kOk:
        break;
    }
  }

  // Figure out the bounds of the indices in the underlying storage and search
  // it.
  uint32_t start = non_null_->CountSetBits(in.start);
  uint32_t end = non_null_->CountSetBits(in.end);
  BitVector res = ReconcileStorageResult(
      op, *non_null_, inner_->SearchValidated(op, sql_val, Range(start, end)),
      in);

  PERFETTO_DCHECK(res.size() == in.end);
  return RangeOrBitVector(std::move(res));
}

void NullOverlay::ChainImpl::IndexSearchValidated(FilterOp op,
                                                  SqlValue sql_val,
                                                  Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "NullOverlay::ChainImpl::IndexSearch");

  if (op == FilterOp::kIsNull) {
    // Partition the vector into all the null indices followed by all the
    // non-null indices.
    auto non_null_it = std::stable_partition(
        indices.tokens.begin(), indices.tokens.end(),
        [this](const Token& t) { return !non_null_->IsSet(t.index); });

    // IndexSearch |inner_| with a vector containing a copy of the (translated)
    // non-null indices.
    Indices non_null{{non_null_it, indices.tokens.end()}, indices.state};
    for (auto& token : non_null.tokens) {
      token.index = non_null_->CountSetBits(token.index);
    }
    inner_->IndexSearch(op, sql_val, non_null);

    // Replace all the original non-null positions with the result from calling
    // IndexSearch.
    auto new_non_null_it =
        indices.tokens.erase(non_null_it, indices.tokens.end());
    indices.tokens.insert(new_non_null_it, non_null.tokens.begin(),
                          non_null.tokens.end());

    // Merge the two sorted index ranges together using the payload as the
    // comparator. This is a required post-condition of IndexSearch.
    std::inplace_merge(indices.tokens.begin(), new_non_null_it,
                       indices.tokens.end(), Token::PayloadComparator());
    return;
  }

  auto keep_only_non_null = [this, &indices]() {
    indices.tokens.erase(
        std::remove_if(
            indices.tokens.begin(), indices.tokens.end(),
            [this](const Token& idx) { return !non_null_->IsSet(idx.index); }),
        indices.tokens.end());
    return;
  };
  if (op == FilterOp::kIsNotNull) {
    switch (inner_->ValidateSearchConstraints(op, sql_val)) {
      case SearchValidationResult::kNoData:
        indices.tokens.clear();
        return;
      case SearchValidationResult::kAllData:
        keep_only_non_null();
        return;
      case SearchValidationResult::kOk:
        break;
    }
  }
  keep_only_non_null();
  for (auto& token : indices.tokens) {
    token.index = non_null_->CountSetBits(token.index);
  }
  inner_->IndexSearchValidated(op, sql_val, indices);
}

void NullOverlay::ChainImpl::StableSort(Token* start,
                                        Token* end,
                                        SortDirection direction) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "NullOverlay::ChainImpl::StableSort");
  Token* middle = std::stable_partition(start, end, [this](const Token& idx) {
    return !non_null_->IsSet(idx.index);
  });
  for (Token* it = middle; it != end; ++it) {
    it->index = non_null_->CountSetBits(it->index);
  }
  inner_->StableSort(middle, end, direction);
  if (direction == SortDirection::kDescending) {
    std::rotate(start, middle, end);
  }
}

void NullOverlay::ChainImpl::Distinct(Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "NullOverlay::ChainImpl::Distinct");
  auto null_tok = UpdateIndicesForInner(indices, *non_null_);

  inner_->Distinct(indices);

  // Add the only null as it is distinct value.
  if (null_tok.has_value()) {
    indices.tokens.push_back(*null_tok);
  }
}

std::optional<Token> NullOverlay::ChainImpl::MaxElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "NullOverlay::ChainImpl::MaxElement");
  auto null_tok = UpdateIndicesForInner(indices, *non_null_);

  std::optional<Token> max_tok = inner_->MaxElement(indices);

  return max_tok ? max_tok : null_tok;
}

std::optional<Token> NullOverlay::ChainImpl::MinElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "NullOverlay::ChainImpl::MinDistinct");
  // The smallest value would be a NULL, so we should just return NULL here.
  auto first_null_it = std::find_if(
      indices.tokens.begin(), indices.tokens.end(),
      [this](const Token& t) { return !non_null_->IsSet(t.index); });

  if (first_null_it != indices.tokens.end()) {
    return *first_null_it;
  }

  // If we didn't find a null in indices we need to update index of each token
  // so they all point to inner and look for the smallest value in the storage.
  for (auto& token : indices.tokens) {
    token.index = non_null_->CountSetBits(token.index);
  }

  return inner_->MinElement(indices);
}

SqlValue NullOverlay::ChainImpl::Get_AvoidUsingBecauseSlow(
    uint32_t index) const {
  return non_null_->IsSet(index)
             ? inner_->Get_AvoidUsingBecauseSlow(non_null_->CountSetBits(index))
             : SqlValue();
}

}  // namespace perfetto::trace_processor::column
