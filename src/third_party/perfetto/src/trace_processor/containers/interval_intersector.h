/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_CONTAINERS_INTERVAL_INTERSECTOR_H_
#define SRC_TRACE_PROCESSOR_CONTAINERS_INTERVAL_INTERSECTOR_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "perfetto/base/logging.h"
#include "src/trace_processor/containers/interval_tree.h"

namespace perfetto::trace_processor {

// Provides functionality for efficient intersection of a set of intervals with
// another interval. Operates in various modes: using interval tree, binary
// search of non overlapping intervals or linear scan if the intervals are
// overlapping but there is no need for interval tree.
class IntervalIntersector {
 public:
  // Mode of intersection. Choosing the mode strongly impacts the performance
  // of intersector.
  enum Mode {
    // Use `IntervalTree` as an underlying implementation.. Would create an
    // interval tree - with complexity of O(N) and memory complexity of O(N).
    // Query cost is O(logN).
    // NOTE: Only use if intervals are overlapping and tree would be queried
    // multiple times.
    kIntervalTree,
    // If the intervals are non overlapping we can use simple binary search.
    // There is no memory cost and algorithmic complexity of O(logN + M), where
    // logN is the cost of binary search and M is the number of results.
    // NOTE: Only use if intervals are non overlapping.
    kBinarySearch,
    // Slightly better then linear scan, we are looking for the first
    // overlapping interval and then doing linear scan of the rest. NOTE: Only
    // use if intervals are overlapping and there would be very few queries.
    kLinearScan
  };

  // Creates an interval tree from the vector of intervals if needed. Otherwise
  // copies the vector of intervals.
  explicit IntervalIntersector(const std::vector<Interval>& sorted_intervals,
                               Mode mode)
      : intervals_(sorted_intervals), mode_(mode) {
    if (sorted_intervals.empty()) {
      mode_ = kBinarySearch;
      return;
    }
    if (mode_ == kIntervalTree) {
      tree = std::make_unique<IntervalTree>(intervals_);
    }
  }

  // Modifies |res| to contain Interval::Id of all intervals that overlap
  // interval (s, e).
  template <typename T>
  void FindOverlaps(uint64_t s, uint64_t e, std::vector<T>& res) const {
    if (mode_ == kIntervalTree) {
      tree->FindOverlaps(s, e, res);
      return;
    }

    bool query_is_instant = s == e;

    auto handle_overlap = [&](const Interval& overlap) {
      if (IsOverlapping(query_is_instant, s, e, overlap)) {
        if constexpr (std::is_same_v<T, Interval>) {
          Interval new_int;
          new_int.id = overlap.id;
          if (query_is_instant) {
            new_int.start = s;
            new_int.end = s;
          } else if (overlap.start == overlap.end) {
            new_int.start = overlap.start;
            new_int.end = overlap.start;
          } else {
            new_int.start = std::max(s, overlap.start);
            new_int.end = std::min(e, overlap.end);
          }
          res.push_back(new_int);
        } else {
          static_assert(std::is_same_v<T, Id>);
          res.push_back(overlap.id);
        }
      }
    };

    if (mode_ == kBinarySearch) {
      // Find the first interval that ends at or after |s|.
      auto it = std::lower_bound(intervals_.begin(), intervals_.end(), s,
                                 [](const Interval& interval, uint64_t start) {
                                   return interval.end < start;
                                 });
      // The previous interval could also overlap.
      if (it != intervals_.begin()) {
        --it;
      }

      if (query_is_instant) {
        // For instant queries, we are interested in intervals that contain
        // |s|, so we can stop once we are past |s|. For range queries, we can
        // stop once the interval starts after the end of the query.
        for (; it != intervals_.end(); ++it) {
          if (it->start > s) {
            break;
          }
          handle_overlap(*it);
        }
        return;
      }

      for (; it != intervals_.end(); ++it) {
        if (it->start >= e) {
          break;
        }
        handle_overlap(*it);
      }
      return;
    }

    PERFETTO_CHECK(mode_ == kLinearScan);
    for (const auto& interval : intervals_) {
      handle_overlap(interval);
    }
  }

  // Helper function to decide which intersector mode would be in given
  // situations. Only use if the number of queries is known.
  static Mode DecideMode(bool is_nonoverlapping, uint32_t queries_count) {
    if (is_nonoverlapping) {
      return kBinarySearch;
    }
    if (queries_count < 5) {
      return kLinearScan;
    }
    return kIntervalTree;
  }

 private:
  const std::vector<Interval>& intervals_;
  Mode mode_;

  // If |use_interval_tree_|.
  std::unique_ptr<IntervalTree> tree;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_CONTAINERS_INTERVAL_INTERSECTOR_H_
