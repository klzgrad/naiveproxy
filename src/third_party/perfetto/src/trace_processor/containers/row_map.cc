/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/containers/row_map.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "src/trace_processor/containers/bit_vector.h"
#include "src/trace_processor/containers/row_map_algorithms.h"

namespace perfetto {
namespace trace_processor {

namespace {

using Range = RowMap::Range;
using OutputIndex = RowMap::OutputIndex;
using Variant = std::variant<Range, BitVector, std::vector<OutputIndex>>;

RowMap Select(Range range, Range selector) {
  PERFETTO_DCHECK(selector.start <= selector.end);
  PERFETTO_DCHECK(selector.end <= range.size());

  return RowMap(range.start + selector.start, range.start + selector.end);
}

RowMap Select(Range range, const BitVector& selector) {
  PERFETTO_DCHECK(selector.size() <= range.size());

  // If |start| == 0 and |selector.size()| <= |end - start| (which is a
  // precondition for this function), the BitVector we generate is going to be
  // exactly |selector|.
  //
  // This is a fast path for the common situation where, post-filtering,
  // SelectRows is called on all the table RowMaps with a BitVector. The self
  // RowMap will always be a range so we expect this case to be hit at least
  // once every filter operation.
  if (range.start == 0u)
    return RowMap(selector.Copy());

  // We only need to resize to |start| + |selector.size()| as we know any rows
  // not covered by |selector| are going to be removed below.
  BitVector bv(range.start, false);
  bv.Resize(range.start + selector.size(), true);

  bv.UpdateSetBits(selector);
  return RowMap(std::move(bv));
}

RowMap Select(Range range, const std::vector<OutputIndex>& selector) {
  std::vector<uint32_t> iv(selector.size());
  for (uint32_t i = 0; i < selector.size(); ++i) {
    PERFETTO_DCHECK(selector[i] < range.size());
    iv[i] = selector[i] + range.start;
  }
  return RowMap(std::move(iv));
}

RowMap Select(const BitVector& bv, Range selector) {
  PERFETTO_DCHECK(selector.end <= bv.CountSetBits());
  if (selector.empty()) {
    return {};
  }
  // If we're simply selecting every element in the bitvector, just
  // return a copy of the BitVector without iterating.
  if (selector.start == 0 && selector.end == bv.CountSetBits()) {
    return RowMap(bv.Copy());
  }
  return RowMap(bv.IntersectRange(bv.IndexOfNthSet(selector.start),
                                  bv.IndexOfNthSet(selector.end - 1) + 1));
}

RowMap Select(const BitVector& bv, const BitVector& selector) {
  BitVector ret = bv.Copy();
  ret.UpdateSetBits(selector);
  return RowMap(std::move(ret));
}

RowMap Select(const BitVector& bv, const std::vector<uint32_t>& selector) {
  // The value of this constant was found by considering the benchmarks
  // |BM_SelectBvWithIvByConvertToIv| and |BM_SelectBvWithIvByIndexOfNthSet|.
  //
  // We use this to find the ratio between |bv.CountSetBits()| and
  // |selector.size()| where |SelectBvWithIvByIndexOfNthSet| was found to be
  // faster than |SelectBvWithIvByConvertToIv|.
  //
  // Note: as of writing this, the benchmarks do not take into account the fill
  // ratio of the BitVector; they assume 50% rate which almost never happens in
  // practice. In the future, we could also take this into account (by
  // considering the ratio between bv.size() and bv.CountSetBits()) but this
  // causes an explosion in the state space for the benchmark so we're not
  // considering this today.
  //
  // The current value of the constant was picked by running these benchmarks on
  // a E5-2690 v4 and finding the crossover point using a spreadsheet.
  constexpr uint32_t kIndexOfSetBitToSelectorRatio = 4;

  // If the selector is larger than a threshold, it's more efficient to convert
  // the entire BitVector to an index vector and use SelectIvWithIv instead.
  if (bv.CountSetBits() / kIndexOfSetBitToSelectorRatio < selector.size()) {
    return RowMap(
        row_map_algorithms::SelectBvWithIvByConvertToIv(bv, selector));
  }
  return RowMap(
      row_map_algorithms::SelectBvWithIvByIndexOfNthSet(bv, selector));
}

RowMap Select(const std::vector<uint32_t>& iv, Range selector) {
  PERFETTO_DCHECK(selector.end <= iv.size());

  std::vector<uint32_t> ret(selector.size());
  for (uint32_t i = selector.start; i < selector.end; ++i) {
    ret[i - selector.start] = iv[i];
  }
  return RowMap(std::move(ret));
}

RowMap Select(const std::vector<uint32_t>& iv, const BitVector& selector) {
  PERFETTO_DCHECK(selector.size() <= iv.size());

  std::vector<uint32_t> copy = iv;
  copy.resize(selector.size());

  uint32_t idx = 0;
  auto it = std::remove_if(
      copy.begin(), copy.end(),
      [&idx, &selector](uint32_t) { return !selector.IsSet(idx++); });
  copy.erase(it, copy.end());
  return RowMap(std::move(copy));
}

RowMap Select(const std::vector<uint32_t>& iv,
              const std::vector<uint32_t>& selector) {
  return RowMap(row_map_algorithms::SelectIvWithIv(iv, selector));
}

// O(N), but 64 times faster than doing it bit by bit, as we compare words in
// BitVectors.
Variant IntersectInternal(BitVector& first, const BitVector& second) {
  first.And(second);
  return std::move(first);
}

// O(1) complexity.
Variant IntersectInternal(Range first, Range second) {
  // If both RowMaps have ranges, we can just take the smallest intersection
  // of them as the new RowMap.
  // We have this as an explicit fast path as this is very common for
  // constraints on id and sorted columns to satisfy this condition.
  OutputIndex start = std::max(first.start, second.start);
  OutputIndex end = std::max(start, std::min(first.end, second.end));
  return Range{start, end};
}

// O(N + k) complexity, where N is the size of |second| and k is the number of
// elements that have to be removed from |first|.
Variant IntersectInternal(std::vector<OutputIndex>& first,
                          const std::vector<OutputIndex>& second) {
  std::unordered_set<OutputIndex> lookup(second.begin(), second.end());
  first.erase(std::remove_if(first.begin(), first.end(),
                             [lookup](OutputIndex ind) {
                               return lookup.find(ind) == lookup.end();
                             }),
              first.end());
  return std::move(first);
}

// O(1) complexity.
Variant IntersectInternal(Range range, const BitVector& bv) {
  return bv.IntersectRange(range.start, range.end);
}

Variant IntersectInternal(BitVector& bv, Range range) {
  return IntersectInternal(range, bv);
}

Variant IntersectInternal(const std::vector<OutputIndex>& index_vec,
                          const BitVector& bv) {
  std::vector<OutputIndex> new_vec(index_vec.begin(), index_vec.end());
  new_vec.erase(std::remove_if(new_vec.begin(), new_vec.end(),
                               [&bv](uint32_t i) { return !bv.IsSet(i); }),
                new_vec.end());
  return std::move(new_vec);
}

Variant IntersectInternal(const BitVector& bv,
                          const std::vector<OutputIndex>& index_vec) {
  return IntersectInternal(index_vec, bv);
}

Variant IntersectInternal(Range range,
                          const std::vector<OutputIndex>& index_vec) {
  std::vector<OutputIndex> new_vec(index_vec.begin(), index_vec.end());
  new_vec.erase(std::remove_if(new_vec.begin(), new_vec.end(),
                               [range](uint32_t i) {
                                 return i < range.start || i >= range.end;
                               }),
                new_vec.end());
  return std::move(new_vec);
}

Variant IntersectInternal(const std::vector<OutputIndex>& index_vec,
                          Range range) {
  return IntersectInternal(range, index_vec);
}

}  // namespace

RowMap::RowMap() : RowMap(Range()) {}

RowMap::RowMap(uint32_t start, uint32_t end) : data_(Range{start, end}) {}

RowMap::RowMap(Variant def) : data_(std::move(def)) {}

RowMap::RowMap(Range r) : data_(r) {}

// Creates a RowMap backed by a BitVector.
RowMap::RowMap(BitVector bit_vector) : data_(std::move(bit_vector)) {}

// Creates a RowMap backed by an std::vector<uint32_t>.
RowMap::RowMap(IndexVector vec) : data_(vec) {}

RowMap RowMap::Copy() const {
  if (const auto* range = std::get_if<Range>(&data_)) {
    return RowMap(*range);
  }
  if (const auto* bv = std::get_if<BitVector>(&data_)) {
    return RowMap(bv->Copy());
  }
  if (const auto* vec = std::get_if<IndexVector>(&data_)) {
    return RowMap(*vec);
  }
  NoVariantMatched();
}

OutputIndex RowMap::Max() const {
  if (const auto* range = std::get_if<Range>(&data_)) {
    return range->end;
  }
  if (const auto* bv = std::get_if<BitVector>(&data_)) {
    return bv->size();
  }
  if (const auto* vec = std::get_if<IndexVector>(&data_)) {
    return vec->empty() ? 0 : *std::max_element(vec->begin(), vec->end()) + 1;
  }
  NoVariantMatched();
}

RowMap RowMap::SelectRowsSlow(const RowMap& selector) const {
  return std::visit(
      [](const auto& def, const auto& selector_def) {
        return Select(def, selector_def);
      },
      data_, selector.data_);
}

void RowMap::Intersect(const RowMap& second) {
  data_ = std::visit(
      [](auto& def, auto& selector_def) {
        return IntersectInternal(def, selector_def);
      },
      data_, second.data_);
}

RowMap::Iterator::Iterator(const RowMap* rm) : rm_(rm) {
  if (const auto* range = std::get_if<Range>(&rm_->data_)) {
    ordinal_ = range->start;
    return;
  }
  if (const auto* bv = std::get_if<BitVector>(&rm_->data_)) {
    results_ = bv->GetSetBitIndices();
    return;
  }
}

}  // namespace trace_processor
}  // namespace perfetto
