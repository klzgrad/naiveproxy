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

#include "src/trace_processor/db/column/id_storage.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/bit_vector.h"
#include "src/trace_processor/db/column/data_layer.h"
#include "src/trace_processor/db/column/storage_layer.h"
#include "src/trace_processor/db/column/types.h"
#include "src/trace_processor/db/column/utils.h"
#include "src/trace_processor/tp_metatrace.h"

#include "protos/perfetto/trace_processor/metatrace_categories.pbzero.h"

namespace perfetto::trace_processor::column {
namespace {

template <typename Comparator>
void IndexSearchWithComparator(uint32_t val, DataLayerChain::Indices& indices) {
  indices.tokens.erase(
      std::remove_if(
          indices.tokens.begin(), indices.tokens.end(),
          [val](const Token& idx) { return !Comparator()(idx.index, val); }),
      indices.tokens.end());
}

}  // namespace

StorageLayer::StoragePtr IdStorage::GetStoragePtr() {
  return Id{};
}

SearchValidationResult IdStorage::ChainImpl::ValidateSearchConstraints(
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
      PERFETTO_FATAL("Invalid constraint");
    case FilterOp::kGlob:
    case FilterOp::kRegex:
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

  if (PERFETTO_UNLIKELY(num_val > std::numeric_limits<uint32_t>::max())) {
    if (op == FilterOp::kLe || op == FilterOp::kLt || op == FilterOp::kNe) {
      return SearchValidationResult::kAllData;
    }
    return SearchValidationResult::kNoData;
  }
  if (PERFETTO_UNLIKELY(num_val < std::numeric_limits<uint32_t>::min())) {
    if (op == FilterOp::kGe || op == FilterOp::kGt || op == FilterOp::kNe) {
      return SearchValidationResult::kAllData;
    }
    return SearchValidationResult::kNoData;
  }

  return SearchValidationResult::kOk;
}

SingleSearchResult IdStorage::ChainImpl::SingleSearch(FilterOp op,
                                                      SqlValue sql_val,
                                                      uint32_t index) const {
  if (sql_val.type != SqlValue::kLong ||
      sql_val.long_value > std::numeric_limits<uint32_t>::max() ||
      sql_val.long_value < std::numeric_limits<uint32_t>::min()) {
    // Because of the large amount of code needing for handling comparisions
    // with doubles or out of range values, just defer to the full search.
    return SingleSearchResult::kNeedsFullSearch;
  }
  auto val = static_cast<uint32_t>(sql_val.long_value);
  switch (op) {
    case FilterOp::kEq:
      return index == val ? SingleSearchResult::kMatch
                          : SingleSearchResult::kNoMatch;
    case FilterOp::kNe:
      return index != val ? SingleSearchResult::kMatch
                          : SingleSearchResult::kNoMatch;
    case FilterOp::kGe:
      return index >= val ? SingleSearchResult::kMatch
                          : SingleSearchResult::kNoMatch;
    case FilterOp::kGt:
      return index > val ? SingleSearchResult::kMatch
                         : SingleSearchResult::kNoMatch;
    case FilterOp::kLe:
      return index <= val ? SingleSearchResult::kMatch
                          : SingleSearchResult::kNoMatch;
    case FilterOp::kLt:
      return index < val ? SingleSearchResult::kMatch
                         : SingleSearchResult::kNoMatch;
    case FilterOp::kIsNotNull:
      return SingleSearchResult::kMatch;
    case FilterOp::kIsNull:
    case FilterOp::kGlob:
    case FilterOp::kRegex:
      return SingleSearchResult::kNoMatch;
  }
  PERFETTO_FATAL("For GCC");
}

RangeOrBitVector IdStorage::ChainImpl::SearchValidated(
    FilterOp op,
    SqlValue sql_val,
    Range search_range) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB, "IdStorage::ChainImpl::Search",
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
    BitVector ret(search_range.start, false);
    ret.Resize(search_range.end, true);
    ret.Clear(val);
    return RangeOrBitVector(std::move(ret));
  }
  return RangeOrBitVector(BinarySearchIntrinsic(op, val, search_range));
}

void IdStorage::ChainImpl::IndexSearchValidated(FilterOp op,
                                                SqlValue sql_val,
                                                Indices& indices) const {
  PERFETTO_TP_TRACE(
      metatrace::Category::DB, "IdStorage::ChainImpl::IndexSearch",
      [&indices, op](metatrace::Record* r) {
        r->AddArg("Count", std::to_string(indices.tokens.size()));
        r->AddArg("Op", std::to_string(static_cast<uint32_t>(op)));
      });

  // It's a valid filter operation if |sql_val| is a double, although it
  // requires special logic.
  if (sql_val.type == SqlValue::kDouble) {
    switch (utils::CompareIntColumnWithDouble(op, &sql_val)) {
      case SearchValidationResult::kAllData:
        return;
      case SearchValidationResult::kNoData:
        indices.tokens.clear();
        return;
      case SearchValidationResult::kOk:
        break;
    }
  }

  auto val = static_cast<uint32_t>(sql_val.AsLong());
  switch (op) {
    case FilterOp::kEq:
      return IndexSearchWithComparator<std::equal_to<>>(val, indices);
    case FilterOp::kNe:
      return IndexSearchWithComparator<std::not_equal_to<>>(val, indices);
    case FilterOp::kLe:
      return IndexSearchWithComparator<std::less_equal<>>(val, indices);
    case FilterOp::kLt:
      return IndexSearchWithComparator<std::less<>>(val, indices);
    case FilterOp::kGt:
      return IndexSearchWithComparator<std::greater<>>(val, indices);
    case FilterOp::kGe:
      return IndexSearchWithComparator<std::greater_equal<>>(val, indices);
    case FilterOp::kIsNotNull:
    case FilterOp::kIsNull:
    case FilterOp::kGlob:
    case FilterOp::kRegex:
      PERFETTO_FATAL("Invalid filter operation");
  }
  PERFETTO_FATAL("FilterOp not matched");
}

Range IdStorage::ChainImpl::BinarySearchIntrinsic(FilterOp op,
                                                  Id val,
                                                  Range range) {
  switch (op) {
    case FilterOp::kEq:
      return {val, val + (range.start <= val && val < range.end)};
    case FilterOp::kLe:
      return {range.start, std::clamp(val + 1, range.start, range.end)};
    case FilterOp::kLt:
      return {range.start, std::clamp(val, range.start, range.end)};
    case FilterOp::kGe:
      return {std::clamp(val, range.start, range.end), range.end};
    case FilterOp::kGt:
      return {std::clamp(val + 1, range.start, range.end), range.end};
    case FilterOp::kIsNotNull:
    case FilterOp::kNe:
    case FilterOp::kIsNull:
    case FilterOp::kGlob:
    case FilterOp::kRegex:
      PERFETTO_FATAL("Invalid filter operation");
  }
  PERFETTO_FATAL("FilterOp not matched");
}

void IdStorage::ChainImpl::StableSort(Token* start,
                                      Token* end,
                                      SortDirection direction) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "IdStorage::ChainImpl::StableSort");
  switch (direction) {
    case SortDirection::kAscending:
      std::stable_sort(start, end, [](const Token& a, const Token& b) {
        return a.index < b.index;
      });
      return;
    case SortDirection::kDescending:
      std::stable_sort(start, end, [](const Token& a, const Token& b) {
        return a.index > b.index;
      });
      return;
  }
  PERFETTO_FATAL("For GCC");
}

void IdStorage::ChainImpl::Distinct(Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB, "IdStorage::ChainImpl::Distinct");
  std::unordered_set<uint32_t> s;
  indices.tokens.erase(
      std::remove_if(
          indices.tokens.begin(), indices.tokens.end(),
          [&s](const Token& idx) { return !s.insert(idx.index).second; }),
      indices.tokens.end());
}

std::optional<Token> IdStorage::ChainImpl::MaxElement(Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "IdStorage::ChainImpl::MaxElement");
  auto tok = std::max_element(
      indices.tokens.begin(), indices.tokens.end(),
      [](const Token& a, const Token& b) { return a.index < b.index; });
  return (tok == indices.tokens.end()) ? std::nullopt
                                       : std::make_optional(*tok);
}

std::optional<Token> IdStorage::ChainImpl::MinElement(Indices& indices) const {
  PERFETTO_TP_TRACE(metatrace::Category::DB,
                    "IdStorage::ChainImpl::MinElement");
  auto tok = std::min_element(
      indices.tokens.begin(), indices.tokens.end(),
      [](const Token& a, const Token& b) { return a.index > b.index; });
  if (tok == indices.tokens.end()) {
    return std::nullopt;
  }
  return *tok;
}

SqlValue IdStorage::ChainImpl::Get_AvoidUsingBecauseSlow(uint32_t index) const {
  return SqlValue::Long(index);
}

}  // namespace perfetto::trace_processor::column
