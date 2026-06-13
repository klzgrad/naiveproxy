/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_UTIL_GALLOPING_SEARCH_H_
#define SRC_TRACE_PROCESSOR_UTIL_GALLOPING_SEARCH_H_

#include <cstdint>

namespace perfetto::trace_processor {

// Galloping (exponential) search optimized for sorted queries.
//
// When searching for multiple keys that are themselves sorted, galloping search
// exploits locality: each search starts from where the previous one ended,
// using exponential probing to quickly find the new range, then binary search
// within that range.
//
// Performance: O(log d) per query where d is the distance between consecutive
// keys. Much faster than repeated std::lower_bound for sorted query batches.
class GallopingSearch {
 public:
  GallopingSearch(const int64_t* data, uint32_t size) : data_(data), n_(size) {}

  // Keys MUST be sorted in ascending order for this to work correctly.
  // Results[i] will contain the lower_bound position for keys[i].
  void BatchedLowerBound(const int64_t* keys,
                         uint32_t num_keys,
                         uint32_t* results) const {
    if (n_ == 0) {
      for (uint32_t i = 0; i < num_keys; ++i) {
        results[i] = 0;
      }
      return;
    }
    uint32_t pos = LowerBound(0, n_, keys[0]);
    results[0] = pos;
    for (uint32_t i = 1; i < num_keys; ++i) {
      pos = GallopForward(pos, keys[i]);
      results[i] = pos;
    }
  }

 private:
  static constexpr uint32_t kLinearScanThreshold = 16;

  uint32_t GallopForward(uint32_t pos, int64_t key) const {
    if (pos >= n_ || data_[pos] >= key) {
      return pos;
    }
    // Start at cache-line granularity (16 elements = 2 cache lines of int64_t)
    // since nearby elements are already paged in when we access data[pos].
    uint32_t step = kLinearScanThreshold;
    uint32_t prev = pos;
    while (pos + step < n_ && data_[pos + step] < key) {
      prev = pos + step;
      step *= 2;
    }
    uint32_t lo = prev + 1;
    uint32_t hi = (pos + step + 1 > n_) ? n_ : pos + step + 1;
    return LowerBound(lo, hi, key);
  }

  uint32_t LowerBound(uint32_t lo, uint32_t hi, int64_t key) const {
    // Binary search until range is small enough for linear scan.
    while (hi - lo > kLinearScanThreshold) {
      uint32_t mid = lo + (hi - lo) / 2;
      if (data_[mid] < key) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    // Linear scan for small ranges.
    while (lo < hi && data_[lo] < key) {
      ++lo;
    }
    return lo;
  }

  const int64_t* data_ = nullptr;
  uint32_t n_ = 0;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_UTIL_GALLOPING_SEARCH_H_
