
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

#include "src/trace_processor/db/column/numeric_storage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
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

using Indices = DataLayerChain::Indices;
using OrderedIndices = DataLayerChain::OrderedIndices;

using NumericValue = std::variant<uint32_t, int32_t, int64_t, double>;

// Using the fact that binary operators in std are operators() of classes, we
// can wrap those classes in variants and use them for std::visit in
// SerialComparators. This helps prevent excess templating and switches.
template <typename T>
using FilterOpVariant = std::variant<std::greater<T>,
                                     std::greater_equal<T>,
                                     std::less<T>,
                                     std::less_equal<T>,
                                     std::equal_to<T>,
                                     std::not_equal_to<T>>;

// Based on SqlValue and ColumnType, casts SqlValue to proper type. Assumes the
// |val| and |type| are correct.
inline NumericValue GetIntegerOrDoubleTypeVariant(ColumnType type,
                                                  SqlValue val) {
  switch (type) {
    case ColumnType::kDouble:
      return val.AsDouble();
    case ColumnType::kInt64:
      return val.AsLong();
    case ColumnType::kInt32:
      return static_cast<int32_t>(val.AsLong());
    case ColumnType::kUint32:
      return static_cast<uint32_t>(val.AsLong());
    case ColumnType::kString:
    case ColumnType::kDummy:
    case ColumnType::kId:
      PERFETTO_FATAL("Invalid type");
  }
  PERFETTO_FATAL("For GCC");
}

// Fetch std binary comparator class based on FilterOp. Can be used in
// std::visit for comparison.
template <typename T>
inline FilterOpVariant<T> GetFilterOpVariant(FilterOp op) {
  switch (op) {
    case FilterOp::kEq:
      return FilterOpVariant<T>(std::equal_to<T>());
    case FilterOp::kNe:
      return FilterOpVariant<T>(std::not_equal_to<T>());
    case FilterOp::kGe:
      return FilterOpVariant<T>(std::greater_equal<T>());
    case FilterOp::kGt:
      return FilterOpVariant<T>(std::greater<T>());
    case FilterOp::kLe:
      return FilterOpVariant<T>(std::less_equal<T>());
    case FilterOp::kLt:
      return FilterOpVariant<T>(std::less<T>());
    case FilterOp::kGlob:
    case FilterOp::kRegex:
    case FilterOp::kIsNotNull:
    case FilterOp::kIsNull:
      PERFETTO_FATAL("Not a valid operation on numeric type.");
  }
  PERFETTO_FATAL("For GCC");
}

uint32_t LowerBoundIntrinsic(const void* vector_ptr,
                             NumericValue val,
                             Range search_range) {
  return std::visit(
      [vector_ptr, search_range](auto val_data) {
        using T = decltype(val_data);
        const T* typed_start =
            static_cast<const std::vector<T>*>(vector_ptr)->data();
        const auto* lower =
            std::lower_bound(typed_start + search_range.start,
                             typed_start + search_range.end, val_data);
        return static_cast<uint32_t>(std::distance(typed_start, lower));
      },
      val);
}

uint32_t UpperBoundIntrinsic(const void* vector_ptr,
                             NumericValue val,
                             Range search_range) {
  return std::visit(
      [vector_ptr, search_range](auto val_data) {
        using T = decltype(val_data);
        const T* typed_start =
            static_cast<const std::vector<T>*>(vector_ptr)->data();
        const auto* upper =
            std::upper_bound(typed_start + search_range.start,
                             typed_start + search_range.end, val_data);
        return static_cast<uint32_t>(std::distance(typed_start, upper));
      },
      val);
}

template <typename T>
void TypedLinearSearch(T typed_val,
                       const T* start,
                       FilterOp op,
                       BitVector::Builder& builder) {
  switch (op) {
    case FilterOp::kEq:
      return utils::LinearSearchWithComparator(typed_val, start,
                                               std::equal_to<T>(), builder);
    case FilterOp::kNe:
      return utils::LinearSearchWithComparator(typed_val, start,
                                               std::not_equal_to<T>(), builder);
    case FilterOp::kLe:
      return utils::LinearSearchWithComparator(typed_val, start,
                                               std::less_equal<T>(), builder);
    case FilterOp::kLt:
      return utils::LinearSearchWithComparator(typed_val, start, std::less<T>(),
                                               builder);
    case FilterOp::kGt:
      return utils::LinearSearchWithComparator(typed_val, start,
                                               std::greater<T>(), builder);
    case FilterOp::kGe:
      return utils::LinearSearchWithComparator(
          typed_val, start, std::greater_equal<T>(), builder);
    case FilterOp::kGlob:
    case FilterOp::kRegex:
    case FilterOp::kIsNotNull:
    case FilterOp::kIsNull:
      PERFETTO_DFATAL("Illegal argument");
  }
}

SearchValidationResult IntColumnWithDouble(FilterOp op, SqlValue* sql_val) {
  double double_val = sql_val->AsDouble();

  // Case when |sql_val| can be interpreted as a SqlValue::Double.
  if (std::equal_to<>()(static_cast<double>(static_cast<int64_t>(double_val)),
                        double_val)) {
    *sql_val = SqlValue::Long(static_cast<int64_t>(double_val));
    return SearchValidationResult::kOk;
  }
  // Logic for when the value is a real double.
  switch (op) {
    case FilterOp::kEq:
      return SearchValidationResult::kNoData;
    case FilterOp::kNe:
      return SearchValidationResult::kAllData;

    case FilterOp::kLe:
    case FilterOp::kGt:
      *sql_val = SqlValue::Long(static_cast<int64_t>(std::floor(double_val)));
      return SearchValidationResult::kOk;

    case FilterOp::kLt:
    case FilterOp::kGe:
      *sql_val = SqlValue::Long(static_cast<int64_t>(std::ceil(double_val)));
      return SearchValidationResult::kOk;

    case FilterOp::kIsNotNull:
    case FilterOp::kIsNull:
    case FilterOp::kGlob:
    case FilterOp::kRegex:
      PERFETTO_FATAL("Invalid filter operation");
  }
  PERFETTO_FATAL("For GCC");
}

SearchValidationResult DoubleColumnWithInt(FilterOp op, SqlValue* sql_val) {
  int64_t i = sql_val->AsLong();
  auto i_as_d = static_cast<double>(i);

  // Case when |sql_val| can be interpreted as a SqlValue::Long.
  if (std::equal_to<int64_t>()(i, static_cast<int64_t>(i_as_d))) {
    *sql_val = SqlValue::Double(i_as_d);
    return SearchValidationResult::kOk;
  }

  // Logic for when the value can't be represented as double.
  switch (op) {
    case FilterOp::kEq:
      return SearchValidationResult::kNoData;
    case FilterOp::kNe:
      return SearchValidationResult::kAllData;

    case FilterOp::kLe:
    case FilterOp::kGt:
      // The first double value smaller than |i|.
      *sql_val = SqlValue::Double(std::nextafter(i_as_d, i - 1));
      return SearchValidationResult::kOk;

    case FilterOp::kLt:
    case FilterOp::kGe:
      // The first double value bigger than |i|.
      *sql_val = SqlValue::Double(std::nextafter(i_as_d, i + 1));
      return SearchValidationResult::kOk;

    case FilterOp::kIsNotNull:
    case FilterOp::kIsNull:
    case FilterOp::kGlob:
    case FilterOp::kRegex:
      PERFETTO_FATAL("Invalid filter operation");
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace

NumericStorageBase::ChainImpl::ChainImpl(const void* vector_ptr,
                                         ColumnType type,
                                         bool is_sorted)
    : vector_ptr_(vector_ptr), storage_type_(type), is_sorted_(is_sorted) {}

SearchValidationResult NumericStorageBase::ChainImpl::ValidateSearchConstraints(
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
  enum ExtremeVal { kTooBig, kTooSmall, kOk };
  ExtremeVal extreme_validator = kOk;

  double num_val = val.type == SqlValue::kLong
                       ? static_cast<double>(val.AsLong())
                       : val.AsDouble();

  switch (storage_type_) {
    case ColumnType::kDouble:
      // Any value would make a sensible comparison with a double.
    case ColumnType::kInt64:
      // TODO(b/307482437): As long as the type is not double there is nothing
      // to verify here, as all values are going to be in the int64_t limits.
      break;
    case ColumnType::kInt32:
      if (num_val > std::numeric_limits<int32_t>::max()) {
        extreme_validator = kTooBig;
        break;
      }
      if (num_val < std::numeric_limits<int32_t>::min()) {
        extreme_validator = kTooSmall;
        break;
      }
      break;
    case ColumnType::kUint32:
      if (num_val > std::numeric_limits<uint32_t>::max()) {
        extreme_validator = kTooBig;
        break;
      }
      if (num_val < std::numeric_limits<uint32_t>::min()) {
        extreme_validator = kTooSmall;
        break;
      }
      break;
    case ColumnType::kString:
    case ColumnType::kDummy:
    case ColumnType::kId:
      break;
  }

  switch (extreme_validator) {
    case kOk:
      return SearchValidationResult::kOk;
    case kTooBig:
      if (op == FilterOp::kLt || op == FilterOp::kLe || op == FilterOp::kNe) {
        return SearchValidationResult::kAllData;
      }
      return SearchValidationResult::kNoData;
    case kTooSmall:
      if (op == FilterOp::kGt || op == FilterOp::kGe || op == FilterOp::kNe) {
        return SearchValidationResult::kAllData;
      }
      return SearchValidationResult::kNoData;
  }

  PERFETTO_FATAL("For GCC");
}

RangeOrBitVector NumericStorageBase::ChainImpl::SearchValidated(
    FilterOp op,
    SqlValue sql_val,
    Range search_range) const {
  PERFETTO_DCHECK(search_range.end <= size());

  PERFETTO_TP_TRACE(
      metatrace::Category::DB, "NumericStorage::ChainImpl::Search",
      [&search_range, op](metatrace::Record* r) {
        r->AddArg("Start", std::to_string(search_range.start));
        r->AddArg("End", std::to_string(search_range.end));
        r->AddArg("Op", std::to_string(static_cast<uint32_t>(op)));
      });

  // Mismatched types - value is double and column is int.
  if (sql_val.type == SqlValue::kDouble &&
      storage_type_ != ColumnType::kDouble) {
    auto ret_opt =
        utils::CanReturnEarly(IntColumnWithDouble(op, &sql_val), search_range);
    if (ret_opt) {
      return RangeOrBitVector(*ret_opt);
    }
  }

  // Mismatched types - column is double and value is int.
  if (sql_val.type != SqlValue::kDouble &&
      storage_type_ == ColumnType::kDouble) {
    auto ret_opt =
        utils::CanReturnEarly(DoubleColumnWithInt(op, &sql_val), search_range);
    if (ret_opt) {
      return RangeOrBitVector(*ret_opt);
    }
  }

  NumericValue val = GetIntegerOrDoubleTypeVariant(storage_type_, sql_val);

  if (is_sorted_) {
    if (op != FilterOp::kNe) {
      return RangeOrBitVector(BinarySearchIntrinsic(op, val, search_range));
    }
    // Not equal is a special operation on binary search, as it doesn't define a
    // range, and rather just `not` range returned with `equal` operation.
    Range r = BinarySearchIntrinsic(FilterOp::kEq, val, search_range);
    BitVector bv(r.start, true);
    bv.Resize(r.end, false);
    bv.Resize(search_range.end, true);
    return RangeOrBitVector(std::move(bv));
  }
  return RangeOrBitVector(LinearSearchInternal(op, val, search_range));
}

void NumericStorageBase::ChainImpl::IndexSearchValidated(
    FilterOp op,
    SqlValue sql_val,
    Indices& indices) const {
  PERFETTO_TP_TRACE(
      metatrace::Category::DB, "NumericStorage::ChainImpl::IndexSearch",
      [&indices, op](metatrace::Record* r) {
        r->AddArg("Count", std::to_string(indices.tokens.size()));
        r->AddArg("Op", std::to_string(static_cast<uint32_t>(op)));
      });

  // Mismatched types - value is double and column is int.
  if (sql_val.type == SqlValue::kDouble &&
      storage_type_ != ColumnType::kDouble) {
    if (utils::CanReturnEarly(IntColumnWithDouble(op, &sql_val), indices)) {
      return;
    }
  }

  // Mismatched types - column is double and value is int.
  if (sql_val.type != SqlValue::kDouble &&
      storage_type_ == ColumnType::kDouble) {
    if (utils::CanReturnEarly(DoubleColumnWithInt(op, &sql_val), indices)) {
      return;
    }
  }

  NumericValue val = GetIntegerOrDoubleTypeVariant(storage_type_, sql_val);
  std::visit(
      [this, &indices, op](auto val) {
        using T = decltype(val);
        auto* start = static_cast<const std::vector<T>*>(vector_ptr_)->data();
        std::visit(
            [start, &indices, val](auto comparator) {
              utils::IndexSearchWithComparator(val, start, indices, comparator);
            },
            GetFilterOpVariant<T>(op));
      },
      val);
}

BitVector NumericStorageBase::ChainImpl::LinearSearchInternal(
    FilterOp op,
    NumericValue val,
    Range range) const {
  BitVector::Builder builder(range.end, range.start);
  if (const auto* u32 = std::get_if<uint32_t>(&val)) {
    const auto* start =
        static_cast<const std::vector<uint32_t>*>(vector_ptr_)->data() +
        range.start;
    TypedLinearSearch(*u32, start, op, builder);
  } else if (const auto* i64 = std::get_if<int64_t>(&val)) {
    const auto* start =
        static_cast<const std::vector<int64_t>*>(vector_ptr_)->data() +
        range.start;
    TypedLinearSearch(*i64, start, op, builder);
  } else if (const auto* i32 = std::get_if<int32_t>(&val)) {
    const auto* start =
        static_cast<const std::vector<int32_t>*>(vector_ptr_)->data() +
        range.start;
    TypedLinearSearch(*i32, start, op, builder);
  } else if (const auto* db = std::get_if<double>(&val)) {
    const auto* start =
        static_cast<const std::vector<double>*>(vector_ptr_)->data() +
        range.start;
    TypedLinearSearch(*db, start, op, builder);
  } else {
    PERFETTO_DFATAL("Invalid");
  }
  return std::move(builder).Build();
}

Range NumericStorageBase::ChainImpl::BinarySearchIntrinsic(
    FilterOp op,
    NumericValue val,
    Range search_range) const {
  switch (op) {
    case FilterOp::kEq:
      return {LowerBoundIntrinsic(vector_ptr_, val, search_range),
              UpperBoundIntrinsic(vector_ptr_, val, search_range)};
    case FilterOp::kLe:
      return {search_range.start,
              UpperBoundIntrinsic(vector_ptr_, val, search_range)};
    case FilterOp::kLt:
      return {search_range.start,
              LowerBoundIntrinsic(vector_ptr_, val, search_range)};
    case FilterOp::kGe:
      return {LowerBoundIntrinsic(vector_ptr_, val, search_range),
              search_range.end};
    case FilterOp::kGt:
      return {UpperBoundIntrinsic(vector_ptr_, val, search_range),
              search_range.end};
    case FilterOp::kNe:
    case FilterOp::kIsNull:
    case FilterOp::kIsNotNull:
    case FilterOp::kGlob:
    case FilterOp::kRegex:
      return {};
  }
  return {};
}

}  // namespace perfetto::trace_processor::column
