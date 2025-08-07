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

#include "src/trace_processor/db/column/utils.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/row_map.h"
#include "src/trace_processor/db/column/data_layer.h"
#include "src/trace_processor/db/column/types.h"

namespace perfetto::trace_processor::column::utils {

SearchValidationResult CompareIntColumnWithDouble(FilterOp op,
                                                  SqlValue* sql_val) {
  double double_val = sql_val->AsDouble();
  if (std::equal_to<>()(
          double_val, static_cast<double>(static_cast<uint32_t>(double_val)))) {
    // If double is the same as uint32_t, we should just "cast" the |sql_val|
    // to be treated as long.
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

std::vector<uint32_t> ToIndexVectorForTests(RangeOrBitVector& r_or_bv) {
  RowMap rm;
  if (r_or_bv.IsBitVector()) {
    rm = RowMap(std::move(r_or_bv).TakeIfBitVector());
  } else {
    Range range = std::move(r_or_bv).TakeIfRange();
    rm = RowMap(range.start, range.end);
  }
  return rm.GetAllIndices();
}

std::vector<uint32_t> ExtractPayloadForTesting(
    const DataLayerChain::Indices& indices) {
  std::vector<uint32_t> payload;
  payload.reserve(indices.tokens.size());
  for (const auto& token : indices.tokens) {
    payload.push_back(token.payload);
  }
  return payload;
}

std::vector<uint32_t> ExtractPayloadForTesting(std::vector<Token>& tokens) {
  std::vector<uint32_t> payload;
  payload.reserve(tokens.size());
  for (const auto& token : tokens) {
    payload.push_back(token.payload);
  }
  return payload;
}

std::optional<Range> CanReturnEarly(SearchValidationResult res, Range range) {
  switch (res) {
    case SearchValidationResult::kOk:
      return std::nullopt;
    case SearchValidationResult::kAllData:
      return range;
    case SearchValidationResult::kNoData:
      return Range();
  }
  PERFETTO_FATAL("For GCC");
}

std::optional<Range> CanReturnEarly(SearchValidationResult res,
                                    uint32_t indices_size) {
  switch (res) {
    case SearchValidationResult::kOk:
      return std::nullopt;
    case SearchValidationResult::kAllData:
      return Range(0, indices_size);
    case SearchValidationResult::kNoData:
      return Range();
  }
  PERFETTO_FATAL("For GCC");
}

bool CanReturnEarly(SearchValidationResult res,
                    DataLayerChain::Indices& indices) {
  switch (res) {
    case SearchValidationResult::kOk:
      return false;
    case SearchValidationResult::kAllData:
      return true;
    case SearchValidationResult::kNoData:
      indices.tokens.clear();
      return true;
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace perfetto::trace_processor::column::utils
