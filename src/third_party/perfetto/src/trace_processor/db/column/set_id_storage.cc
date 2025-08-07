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

#include "src/trace_processor/db/column/set_id_storage.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/bit_vector.h"
#include "src/trace_processor/db/column/data_layer.h"
#include "src/trace_processor/db/column/types.h"
#include "src/trace_processor/db/column/utils.h"
#include "src/trace_processor/tp_metatrace.h"

#include "protos/perfetto/trace_processor/metatrace_categories.pbzero.h"

namespace perfetto::trace_processor::column {
namespace {

using SetId = SetIdStorage::SetId;

uint32_t UpperBoundIntrinsic(const SetId* data, SetId val, Range range) {
  for (uint32_t i = std::max(range.start, val); i < range.end; i++) {
    if (data[i] > val) {
      return i;
    }
  }
  return range.end;
}

uint32_t LowerBoundIntrinsic(const SetId* data, SetId id, Range range) {
  if (data[range.start] == id) {
    return range.start;
  }
  if (range.Contains(id) && data[id] == id) {
    return id;
  }
  // If none of the above are true, than |id| is not present in data, so we need
  // to look for the first value higher than |id|.
  return UpperBoundIntrinsic(data, id, range);
}

}  // namespace

SetIdStorage::StoragePtr SetIdStorage::GetStoragePtr() {
  return values_->data();
}

SetIdStorage::ChainImpl::ChainImpl(const std::vector<uint32_t>* values)
    : values_(values) {}

SingleSearchResult SetIdStorage::ChainImpl::SingleSearch(FilterOp op,
                                                         SqlValue sql_val,
                                                         uint32_t i) const {
  return utils::SingleSearchNumeric(op, (*values_)[i], sql_val);
}

SearchValidationResult SetIdStorage::ChainImpl::ValidateSearchConstraints(
    FilterOp op,
    SqlValue val) const {
  // NULL checks.
  if (PERFETTO_UNLIKELY(val.is_null())) {
    if (op == FilterOp::kIsNotNull) {
      return SearchValidationResult::kAllData;
    }
    return SearchValidationResult::kNoData;
  }

  // FilterOp checks. Switch so that we get a warning if new FilterOp is not
  // handled.
  switch (op) {
    case FilterOp::kEq:
    case FilterOp::kNe:
    case FilterOp::kLt:
    case FilterOp::kLe:
    case FilterOp::kGt:
    case FilterOp::kGe:
      break;
    case FilterOp::kIsNull:
    case FilterOp::kIsNotNull:
      PERFETTO_FATAL("Invalid constraints.");
    case FilterOp::kGlob:
    case FilterOp::kRegex:
      return SearchValidationResult::kNoData;
  }

  if (PERFETTO_UNLIKELY(values_->empty())) {
    return SearchValidationResult::kNoData;
  }

  // Type checks.
  switch (val.type) {
    case SqlValue::kNull:
    case SqlValue::kLong:
    case SqlValue::kDouble:
      break;
    case SqlValue::kString:
      // Any string is always more than any numeric.
      if (op == FilterOp::kLt || op == FilterOp::kLe) {
        return SearchValidationResult::kAllData;
      }
      return SearchValidationResult::kNoData;
    case SqlValue::kBytes:
      return SearchValidationResult::kNoData;
  }

  // Bounds of the value.
  double num_val = val.type == SqlValue::kLong
                       ? static_cast<double>(val.AsLong())
                       : val.AsDouble();

  // As values are sorted, we can cover special cases for when |num_val| is
  // bigger than the last value and smaller than the first one.
  if (PERFETTO_UNLIKELY(num_val > values_->back())) {
    if (op == FilterOp::kLe || op == FilterOp::kLt || op == FilterOp::kNe) {
      return SearchValidationResult::kAllData;
    }
    return SearchValidationResult::kNoData;
  }
  if (PERFETTO_UNLIKELY(num_val < values_->front())) {
    if (op == FilterOp::kGe || op == FilterOp::kGt || op == FilterOp::kNe) {
      return SearchValidationResult::kAllData;
    }
    return SearchValidationResult::kNoData;
  }

  return SearchValidationResult::kOk;
}

RangeOrBitVector SetIdStorage::ChainImpl::SearchValidated(
    FilterOp op,
    SqlValue sql_val,
    Range search_range) const {
  PERFETTO_DCHECK(search_range.end <= size());

  PERFETTO_TP_TRACE(metatrace::Category::DB, "SetIdStorage::ChainImpl::Search",
                    [&search_range, op](metatrace::Record* r) {
                      r->AddArg("Start", std::to_string(search_range.start));
                      r->AddArg("End", std::to_string(search_range.end));
                      r->AddArg("Op",
                                std::to_string(static_cast<uint32_t>(op)));
                    });

  // It's a valid filter operation if |sql_val| is a double, although it
  // requires special logic.
  if (sql_val.type == SqlValue::kDouble) {
    switch (utils::CompareIntColumnWithDouble(op, &sql_val)) {
      case SearchValidationResult::kOk:
        break;
      case SearchValidationResult::kAllData:
        return RangeOrBitVector(Range(0, search_range.end));
      case SearchValidationResult::kNoData:
        return RangeOrBitVector(Range());
    }
  }

  auto val = static_cast<uint32_t>(sql_val.AsLong());
  if (op == FilterOp::kNe) {
    // Not equal is a special operation on binary search, as it doesn't define a
    // range, and rather just `not` range returned with `equal` operation.
    Range eq_range = BinarySearchIntrinsic(FilterOp::kEq, val, search_range);
    BitVector bv(search_range.start, false);
    bv.Resize(eq_range.start, true);
    bv.Resize(eq_range.end, false);
    bv.Resize(search_range.end, true);
    return RangeOrBitVector(std::move(bv));
  }
  return RangeOrBitVector(BinarySearchIntrinsic(op, val, search_range));
}

void SetIdStorage::ChainImpl::IndexSearchValidated(FilterOp op,
                                                   SqlValue sql_val,
                                                   Indices& indices) const {
  PERFETTO_TP_TRACE(
      metatrace::Category::DB, "SetIdStorage::ChainImpl::IndexSearch",
      [&indices, op](metatrace::Record* r) {
        r->AddArg("Count", std::to_string(indices.tokens.size()));
        r->AddArg("Op", std::to_string(static_cast<uint32_t>(op)));
      });

  // It's a valid filter operation if |sql_val| is a double, although it
  // requires special logic.
  if (sql_val.type == SqlValue::kDouble) {
    if (utils::CanReturnEarly(utils::CompareIntColumnWithDouble(op, &sql_val),
                              indices)) {
      return;
    }
  }

  // TODO(mayzner): Instead of utils::IndexSearchWithComparator, use the
  // property of SetId data - that for each index i, data[i] <= i.
  auto val = static_cast<uint32_t>(sql_val.AsLong());
  switch (op) {
    case FilterOp::kEq:
      utils::IndexSearchWithComparator(val, values_->data(), indices,
                                       std::equal_to<>());
      break;
    case FilterOp::kNe:
      utils::IndexSearchWithComparator(val, values_->data(), indices,
                                       std::not_equal_to<>());
      break;
    case FilterOp::kLe:
      utils::IndexSearchWithComparator(val, values_->data(), indices,
                                       std::less_equal<>());
      break;
    case FilterOp::kLt:
      utils::IndexSearchWithComparator(val, values_->data(), indices,
                                       std::less<>());
      break;
    case FilterOp::kGt:
      utils::IndexSearchWithComparator(val, values_->data(), indices,
                                       std::greater<>());
      break;
    case FilterOp::kGe:
      utils::IndexSearchWithComparator(val, values_->data(), indices,
                                       std::greater_equal<>());
      break;
    case FilterOp::kIsNotNull:
    case FilterOp::kIsNull:
    case FilterOp::kGlob:
    case FilterOp::kRegex:
      PERFETTO_FATAL("Illegal argument");
  }
}

Range SetIdStorage::ChainImpl::BinarySearchIntrinsic(FilterOp op,
                                                     SetId val,
                                                     Range range) const {
  switch (op) {
    case FilterOp::kEq: {
      if ((*values_)[val] != val) {
        return {};
      }
      uint32_t start = std::max(val, range.start);
      uint32_t end = UpperBoundIntrinsic(values_->data(), val, range);
      return {std::min(start, end), end};
    }
    case FilterOp::kLe: {
      return {range.start, UpperBoundIntrinsic(values_->data(), val, range)};
    }
    case FilterOp::kLt:
      return {range.start, LowerBoundIntrinsic(values_->data(), val, range)};
    case FilterOp::kGe:
      return {LowerBoundIntrinsic(values_->data(), val, range), range.end};
    case FilterOp::kGt:
      return {UpperBoundIntrinsic(values_->data(), val, range), range.end};
    case FilterOp::kIsNotNull:
      return range;
    case FilterOp::kNe:
      PERFETTO_FATAL("Shouldn't be called");
    case FilterOp::kIsNull:
    case FilterOp::kGlob:
    case FilterOp::kRegex:
      return {};
  }
  return {};
}

void SetIdStorage::ChainImpl::StableSort(Token* start,
                                         Token* end,
                                         SortDirection direction) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "SetIdStorage::ChainImpl::StableSort");
  switch (direction) {
    case SortDirection::kAscending:
      std::stable_sort(start, end, [this](const Token& a, const Token& b) {
        return (*values_)[a.index] < (*values_)[b.index];
      });
      break;
    case SortDirection::kDescending:
      std::stable_sort(start, end, [this](const Token& a, const Token& b) {
        return (*values_)[a.index] > (*values_)[b.index];
      });
      break;
  }
}

void SetIdStorage::ChainImpl::Distinct(Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "SetIdStorage::ChainImpl::Distinct");
  std::unordered_set<uint32_t> s;
  indices.tokens.erase(
      std::remove_if(indices.tokens.begin(), indices.tokens.end(),
                     [&s, this](const Token& idx) {
                       return !s.insert((*values_)[idx.index]).second;
                     }),
      indices.tokens.end());
}

std::optional<Token> SetIdStorage::ChainImpl::MaxElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "SetIdStorage::ChainImpl::MaxElement");

  auto tok =
      std::max_element(indices.tokens.begin(), indices.tokens.end(),
                       [this](const Token& t1, const Token& t2) {
                         return (*values_)[t1.index] < (*values_)[t2.index];
                       });

  if (tok == indices.tokens.end()) {
    return std::nullopt;
  }
  return *tok;
}

std::optional<Token> SetIdStorage::ChainImpl::MinElement(
    Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "SetIdStorage::ChainImpl::MinElement");
  auto tok =
      std::min_element(indices.tokens.begin(), indices.tokens.end(),
                       [this](const Token& t1, const Token& t2) {
                         return (*values_)[t1.index] < (*values_)[t2.index];
                       });
  if (tok == indices.tokens.end()) {
    return std::nullopt;
  }

  return *tok;
}

SqlValue SetIdStorage::ChainImpl::Get_AvoidUsingBecauseSlow(
    uint32_t index) const {
  return SqlValue::Long((*values_)[index]);
}

}  // namespace perfetto::trace_processor::column
