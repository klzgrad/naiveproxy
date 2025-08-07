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

#include "src/trace_processor/db/column/fake_storage.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/bit_vector.h"
#include "src/trace_processor/db/column/data_layer.h"
#include "src/trace_processor/db/column/types.h"

namespace perfetto::trace_processor::column {

FakeStorageChain::FakeStorageChain(uint32_t size,
                                   SearchStrategy strategy,
                                   Range range,
                                   BitVector bv)
    : size_(size),
      strategy_(strategy),
      range_(range),
      bit_vector_(std::move(bv)) {}

SingleSearchResult FakeStorageChain::SingleSearch(FilterOp,
                                                  SqlValue,
                                                  uint32_t i) const {
  PERFETTO_CHECK(i < size_);
  switch (strategy_) {
    case kAll:
      return SingleSearchResult::kMatch;
    case kNone:
      return SingleSearchResult::kNoMatch;
    case kBitVector:
      return bit_vector_.IsSet(i) ? SingleSearchResult::kMatch
                                  : SingleSearchResult::kNoMatch;
    case kRange:
      return range_.Contains(i) ? SingleSearchResult::kMatch
                                : SingleSearchResult::kNoMatch;
  }
  PERFETTO_FATAL("For GCC");
}

SearchValidationResult FakeStorageChain::ValidateSearchConstraints(
    FilterOp,
    SqlValue) const {
  return SearchValidationResult::kOk;
}

RangeOrBitVector FakeStorageChain::SearchValidated(FilterOp,
                                                   SqlValue,
                                                   Range in) const {
  switch (strategy_) {
    case kAll:
      return RangeOrBitVector(in);
    case kNone:
      return RangeOrBitVector(Range());
    case kRange:
      return RangeOrBitVector(Range(std::max(in.start, range_.start),
                                    std::min(in.end, range_.end)));
    case kBitVector: {
      BitVector intersection = bit_vector_.IntersectRange(in.start, in.end);
      intersection.Resize(in.end, false);
      return RangeOrBitVector(std::move(intersection));
    }
  }
  PERFETTO_FATAL("For GCC");
}

void FakeStorageChain::IndexSearchValidated(FilterOp,
                                            SqlValue,
                                            Indices& indices) const {
  switch (strategy_) {
    case kAll:
      return;
    case kNone:
      indices.tokens.clear();
      return;
    case kRange:
      indices.tokens.erase(
          std::remove_if(indices.tokens.begin(), indices.tokens.end(),
                         [this](const Token& token) {
                           return !range_.Contains(token.index);
                         }),
          indices.tokens.end());
      return;
    case kBitVector:
      indices.tokens.erase(
          std::remove_if(indices.tokens.begin(), indices.tokens.end(),
                         [this](const Token& token) {
                           return !bit_vector_.IsSet(token.index);
                         }),
          indices.tokens.end());
      return;
  }
  PERFETTO_FATAL("For GCC");
}

void FakeStorageChain::Distinct(Indices&) const {
  // Fake storage shouldn't implement Distinct as it's not a binary (this index
  // passes or not) operation on a column.
  PERFETTO_FATAL("Not implemented");
}

std::optional<Token> FakeStorageChain::MaxElement(Indices&) const {
  PERFETTO_FATAL("Not implemented");
}
std::optional<Token> FakeStorageChain::MinElement(Indices&) const {
  PERFETTO_FATAL("Not implemented");
}

void FakeStorageChain::StableSort(Token*, Token*, SortDirection) const {
  PERFETTO_FATAL("Not implemented");
}

SqlValue FakeStorageChain::Get_AvoidUsingBecauseSlow(uint32_t) const {
  PERFETTO_FATAL("Not implemented");
}

}  // namespace perfetto::trace_processor::column
