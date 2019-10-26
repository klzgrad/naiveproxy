// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_QUARTC_INTERVAL_COUNTER_H_
#define QUICHE_QUIC_QUARTC_QUARTC_INTERVAL_COUNTER_H_

#include <stddef.h>
#include <vector>

#include "net/third_party/quiche/src/quic/core/quic_interval.h"
#include "net/third_party/quiche/src/quic/core/quic_interval_set.h"

namespace quic {

// QuartcIntervalCounter counts the number of times each value appears within
// a set of potentially overlapping intervals.
//
// QuartcIntervalCounter is not intended for widespread use.  Consider replacing
// it with a full interval-map if more use cases arise.
//
// QuartcIntervalCounter is only suitable for cases where the maximum count is
// expected to remain low.  (For example, counting the number of times the same
// portions of stream data are lost.)  It is inefficient when the maximum count
// becomes high.
template <typename T>
class QuartcIntervalCounter {
 public:
  // Adds |interval| to the counter.  The count associated with each value in
  // |interval| is incremented by one.  |interval| may overlap with previous
  // intervals added to the counter.
  //
  // For each possible value:
  //  - If the value is present in both |interval| and the counter, the count
  //  associated with that value is incremented by one.
  //  - If the value is present in |interval| but not counter, the count
  //  associated with that value is set to one (incremented from zero).
  //  - If the value is absent from |interval|, the count is unchanged.
  //
  // Time complexity is O(|MaxCount| * the complexity of adding an interval to a
  // QuicIntervalSet).
  void AddInterval(QuicInterval<T> interval);

  // Removes an interval from the counter.  This method may be called to prune
  // irrelevant intervals from the counter.  This is useful to prevent unbounded
  // growth.
  //
  // Time complexity is O(|MaxCount| * the complexity of removing an interval
  // from a QuicIntervalSet).
  void RemoveInterval(QuicInterval<T> interval);

  // Returns the maximum number of times any single value has appeared in
  // intervals added to the counter.
  //
  // Time complexity is constant.
  size_t MaxCount() const { return intervals_by_count_.size(); }

  // Returns the maximum number of times a particular value has appeared in
  // intervals added to the counter.
  //
  // Time complexity is O(|MaxCount| * log(number of non-contiguous intervals)).
  size_t Count(const T& value) const;

 private:
  // Each entry in this vector represents the intervals of values counted at
  // least i + 1 times, where i is the index of the entry.
  //
  // Whenever an interval is added to the counter, each value in the interval is
  // added to the first entry which does not already contain that value.  If
  // part of an interval is already present in the last entry, a new entry is
  // added containing that part.
  //
  // Note that this means each value present in one of the interval sets will be
  // present in all previous sets.
  std::vector<QuicIntervalSet<T>> intervals_by_count_;
};

template <typename T>
void QuartcIntervalCounter<T>::AddInterval(QuicInterval<T> interval) {
  // After the Nth iteration, |leftover| contains the parts of |interval| that
  // are already present in the first N entries.  These parts of |interval| have
  // been added to the counter more than N times.
  QuicIntervalSet<T> leftover(interval);
  for (auto& intervals : intervals_by_count_) {
    QuicIntervalSet<T> tmp = leftover;
    leftover.Intersection(intervals);
    intervals.Union(tmp);
  }

  // Whatever ranges are still in |leftover| are already in all the entries
  // Add a new entry containing |leftover|.
  if (!leftover.Empty()) {
    intervals_by_count_.push_back(leftover);
  }
}

template <typename T>
void QuartcIntervalCounter<T>::RemoveInterval(QuicInterval<T> interval) {
  // Remove the interval from each entry in the vector, popping any entries that
  // become empty.
  for (size_t i = intervals_by_count_.size(); i > 0; --i) {
    intervals_by_count_[i - 1].Difference(interval);
    if (intervals_by_count_[i - 1].Empty()) {
      intervals_by_count_.pop_back();
    }
  }
}

template <typename T>
size_t QuartcIntervalCounter<T>::Count(const T& value) const {
  // The index of the last entry containing |value| gives its count.
  for (size_t i = intervals_by_count_.size(); i > 0; --i) {
    if (intervals_by_count_[i - 1].Contains(value)) {
      return i;
    }
  }
  return 0;
}

}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_QUARTC_INTERVAL_COUNTER_H_
