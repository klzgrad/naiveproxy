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

#include "src/trace_processor/db/column/dense_null_overlay.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
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
using Indices = DataLayerChain::Indices;

std::optional<Token> RemoveAllNullsAndReturnTheFirstOne(
    Indices& indices,
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
  return null_tok;
}
}  // namespace

void DenseNullOverlay::Flatten(uint32_t* start,
                               const uint32_t* end,
                               uint32_t stride) {
  for (uint32_t* it = start; it < end; it += stride) {
    if (!non_null_->IsSet(*it)) {
      *it = std::numeric_limits<uint32_t>::max();
    }
  }
}

DenseNullOverlay::ChainImpl::ChainImpl(std::unique_ptr<DataLayerChain> inner,
                                       const BitVector* non_null)
    : inner_(std::move(inner)), non_null_(non_null) {}

SingleSearchResult DenseNullOverlay::ChainImpl::SingleSearch(
    FilterOp op,
    SqlValue sql_val,
    uint32_t index) const {
  switch (op) {
    case FilterOp::kIsNull:
      return non_null_->IsSet(index) ? inner_->SingleSearch(op, sql_val, index)
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
      return non_null_->IsSet(index) ? inner_->SingleSearch(op, sql_val, index)
                                     : SingleSearchResult::kNoMatch;
  }
  PERFETTO_FATAL("For GCC");
}

SearchValidationResult DenseNullOverlay::ChainImpl::ValidateSearchConstraints(
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

RangeOrBitVector DenseNullOverlay::ChainImpl::SearchValidated(FilterOp op,
                                                              SqlValue sql_val,
                                                              Range in) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "DenseNullOverlay::ChainImpl::Search");

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

  RangeOrBitVector inner_res = inner_->SearchValidated(op, sql_val, in);
  BitVector res;
  if (inner_res.IsRange()) {
    // If the inner storage returns a range, mask out the appropriate values in
    // |non_null_| which matches the range. Then, resize to |in.end| as this
    // is mandated by the API contract of |Storage::Search|.
    Range inner_range = std::move(inner_res).TakeIfRange();
    PERFETTO_DCHECK(inner_range.empty() || inner_range.end <= in.end);
    PERFETTO_DCHECK(inner_range.empty() || inner_range.start >= in.start);
    res = non_null_->IntersectRange(inner_range.start, inner_range.end);
    res.Resize(in.end, false);
  } else {
    res = std::move(inner_res).TakeIfBitVector();
  }

  if (op == FilterOp::kIsNull) {
    // For IS NULL, we need to add any rows in |non_null_| which are zeros: we
    // do this by taking the appropriate number of rows, inverting it and then
    // bitwise or-ing the result with it.
    BitVector non_null_copy = non_null_->Copy();
    non_null_copy.Resize(in.end);
    non_null_copy.Not();
    res.Or(non_null_copy);
  } else {
    // For anything else, we just need to ensure that any rows which are null
    // are removed as they would not match.
    res.And(*non_null_);
  }

  PERFETTO_DCHECK(res.size() == in.end);
  return RangeOrBitVector(std::move(res));
}

void DenseNullOverlay::ChainImpl::IndexSearchValidated(FilterOp op,
                                                       SqlValue sql_val,
                                                       Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "DenseNullOverlay::ChainImpl::IndexSearch");

  if (op == FilterOp::kIsNull) {
    // Partition the vector into all the null indices followed by all the
    // non-null indices.
    auto non_null_it = std::stable_partition(
        indices.tokens.begin(), indices.tokens.end(),
        [this](const Token& t) { return !non_null_->IsSet(t.index); });

    // IndexSearch |inner_| with a vector containing a copy of the non-null
    // indices.
    Indices non_null{{non_null_it, indices.tokens.end()}, indices.state};
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
  inner_->IndexSearchValidated(op, sql_val, indices);
}

void DenseNullOverlay::ChainImpl::StableSort(Token* start,
                                             Token* end,
                                             SortDirection direction) const {
  Token* it = std::stable_partition(start, end, [this](const Token& idx) {
    return !non_null_->IsSet(idx.index);
  });
  inner_->StableSort(it, end, direction);
  if (direction == SortDirection::kDescending) {
    std::rotate(start, it, end);
  }
}

void DenseNullOverlay::ChainImpl::Distinct(Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "DenseNullOverlay::ChainImpl::Distinct");
  std::optional<Token> null_tok =
      RemoveAllNullsAndReturnTheFirstOne(indices, *non_null_);

  inner_->Distinct(indices);

  // Add the only null as it is distinct value.
  if (null_tok.has_value()) {
    indices.tokens.push_back(*null_tok);
  }
}

std::optional<Token> DenseNullOverlay::ChainImpl::MaxElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "DenseNullOverlay::ChainImpl::MaxElement");
  std::optional<Token> null_tok =
      RemoveAllNullsAndReturnTheFirstOne(indices, *non_null_);

  std::optional<Token> max_val = inner_->MaxElement(indices);

  return max_val ? max_val : null_tok;
}

std::optional<Token> DenseNullOverlay::ChainImpl::MinElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "DenseNullOverlay::ChainImpl::MinElement");
  // Return the first NULL if found.
  auto first_null_it = std::find_if(
      indices.tokens.begin(), indices.tokens.end(),
      [this](const Token& t) { return !non_null_->IsSet(t.index); });

  return (first_null_it == indices.tokens.end()) ? inner_->MinElement(indices)
                                                 : *first_null_it;
}

SqlValue DenseNullOverlay::ChainImpl::Get_AvoidUsingBecauseSlow(
    uint32_t index) const {
  return non_null_->IsSet(index) ? inner_->Get_AvoidUsingBecauseSlow(index)
                                 : SqlValue();
}

}  // namespace perfetto::trace_processor::column
