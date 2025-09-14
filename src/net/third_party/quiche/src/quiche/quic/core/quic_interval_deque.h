// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_INTERVAL_DEQUE_H_
#define QUICHE_QUIC_CORE_QUIC_INTERVAL_DEQUE_H_

#include <algorithm>
#include <optional>

#include "quiche/quic/core/quic_interval.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/quiche_circular_deque.h"

namespace quic {

namespace test {
class QuicIntervalDequePeer;
}  // namespace test

// QuicIntervalDeque<T, C> is a templated wrapper container, wrapping random
// access data structures. The wrapper allows items to be added to the
// underlying container with intervals associated with each item. The intervals
// _should_ be added already sorted and represent searchable indices. The search
// is optimized for sequential usage.
//
// As the intervals are to be searched sequentially the search for the next
// interval can be achieved in O(1), by simply remembering the last interval
// consumed. The structure also checks for an "off-by-one" use case wherein the
// |cached_index_| is off by one index as the caller didn't call operator |++|
// to increment the index. Other intervals can be found in O(log(n)) as they are
// binary searchable. A use case for this structure is packet buffering: Packets
// are sent sequentially but can sometimes needed for retransmission. The
// packets and their payloads are added with associated intervals representing
// data ranges they carry. When a user asks for a particular interval it's very
// likely they are requesting the next sequential interval, receiving it in O(1)
// time. Updating the sequential index is done automatically through the
// |DataAt| method and its iterator operator |++|.
//
// The intervals are represented using the usual C++ STL convention, namely as
// the half-open QuicInterval [min, max). A point p is considered to be
// contained in the QuicInterval iff p >= min && p < max. One consequence of
// this definition is that for any non-empty QuicInterval, min is contained in
// the QuicInterval but max is not. There is no canonical representation for the
// empty QuicInterval; and empty intervals are forbidden from being added to
// this container as they would be unsearchable.
//
// The type T is required to be copy-constructable or move-constructable. The
// type T is also expected to have an |interval()| method returning a
// QuicInterval<std::size> for the particular value. The type C is required to
// be a random access container supporting the methods |pop_front|, |push_back|,
// |operator[]|, |size|, and iterator support for |std::lower_bound| eg. a
// |deque| or |vector|.
//
// The QuicIntervalDeque<T, C>, like other C++ STL random access containers,
// doesn't have any explicit support for any equality operators.
//
//
// Examples with internal state:
//
//   // An example class to be stored inside the Interval Deque.
//   struct IntervalVal {
//     const int32_t val;
//     const size_t interval_begin, interval_end;
//     QuicInterval<size_t> interval();
//   };
//   typedef IntervalVal IV;
//   QuicIntervialDeque<IntervalValue> deque;
//
//   // State:
//   //   cached_index -> None
//   //   container -> {}
//
//   // Add interval items
//   deque.PushBack(IV(val: 0, interval_begin: 0, interval_end: 10));
//   deque.PushBack(IV(val: 1, interval_begin: 20, interval_end: 25));
//   deque.PushBack(IV(val: 2, interval_begin: 25, interval_end: 30));
//
//   // State:
//   //   cached_index -> 0
//   //   container -> {{0, [0, 10)}, {1, [20, 25)}, {2, [25, 30)}}
//
//   // Look for 0 and return [0, 10). Time: O(1)
//   auto it = deque.DataAt(0);
//   assert(it->val == 0);
//   it++;  // Increment and move the |cached_index_| over [0, 10) to [20, 25).
//   assert(it->val == 1);
//
//   // State:
//   //   cached_index -> 1
//   //   container -> {{0, [0, 10)}, {1, [20, 25)}, {2, [25, 30)}}
//
//   // Look for 20 and return [20, 25). Time: O(1)
//   auto it = deque.DataAt(20); // |cached_index_| remains unchanged.
//   assert(it->val == 1);
//
//   // State:
//   //   cached_index -> 1
//   //   container -> {{0, [0, 10)}, {1, [20, 25)}, {2, [25, 30)}}
//
//   // Look for 15 and return deque.DataEnd(). Time: O(log(n))
//   auto it = deque.DataAt(15); // |cached_index_| remains unchanged.
//   assert(it == deque.DataEnd());
//
//   // Look for 25 and return [25, 30). Time: O(1) with off-by-one.
//   auto it = deque.DataAt(25); // |cached_index_| is updated to 2.
//   assert(it->val == 2);
//   it++; // |cached_index_| is set to |None| as all data has been iterated.
//
//
//   // State:
//   //   cached_index -> None
//   //   container -> {{0, [0, 10)}, {1, [20, 25)}, {2, [25, 30)}}
//
//   // Look again for 0 and return [0, 10). Time: O(log(n))
//   auto it = deque.DataAt(0);
//
//
//   deque.PopFront();  // Pop -> {0, [0, 10)}
//
//   // State:
//   //   cached_index -> None
//   //   container -> {{1, [20, 25)}, {2, [25, 30)}}
//
//   deque.PopFront();  // Pop -> {1, [20, 25)}
//
//   // State:
//   //   cached_index -> None
//   //   container -> {{2, [25, 30)}}
//
//   deque.PushBack(IV(val: 3, interval_begin: 35, interval_end: 50));
//
//   // State:
//   //   cached_index -> 1
//   //   container -> {{2, [25, 30)}, {3, [35, 50)}}

template <class T, class C = quiche::QuicheCircularDeque<T>>
class QUICHE_NO_EXPORT QuicIntervalDeque {
 public:
  // `Iterator` satisfies the requirements for LegacyRandomAccessIterator
  // for efficient std::lower_bound() calls.
  class QUICHE_NO_EXPORT Iterator {
   public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    // Every increment of the iterator will increment the |cached_index_| if
    // |index_| is larger than the current |cached_index_|. |index_| is bounded
    // at |deque_.size()| and any attempt to increment above that will be
    // ignored. Once an iterator has iterated all elements the |cached_index_|
    // will be reset.
    Iterator(std::size_t index, QuicIntervalDeque* deque)
        : index_(index), deque_(deque) {}
    // Only the ++ operator attempts to update the cached index. Other operators
    // are used by |lower_bound| to binary search.
    Iterator& operator++() {
      // Don't increment when we are at the end.
      const std::size_t container_size = deque_->container_.size();
      if (index_ >= container_size) {
        QUIC_BUG(QuicIntervalDeque_operator_plus_plus_iterator_out_of_bounds)
            << "Iterator out of bounds.";
        return *this;
      }
      index_++;
      if (deque_->cached_index_.has_value()) {
        const std::size_t cached_index = *deque_->cached_index_;
        // If all items are iterated then reset the |cached_index_|
        if (index_ == container_size) {
          deque_->cached_index_.reset();
        } else {
          // Otherwise the new |cached_index_| is the max of itself and |index_|
          if (cached_index < index_) {
            deque_->cached_index_ = index_;
          }
        }
      }
      return *this;
    }
    Iterator operator++(int) {
      Iterator copy = *this;
      ++(*this);
      return copy;
    }
    Iterator& operator--() {
      if (index_ == 0) {
        QUIC_BUG(QuicIntervalDeque_operator_minus_minus_iterator_out_of_bounds)
            << "Iterator out of bounds.";
        return *this;
      }
      index_--;
      return *this;
    }
    Iterator operator--(int) {
      Iterator copy = *this;
      --(*this);
      return copy;
    }
    reference operator*() { return deque_->container_[index_]; }
    reference operator*() const { return deque_->container_[index_]; }
    pointer operator->() { return &deque_->container_[index_]; }
    bool operator==(const Iterator& rhs) const {
      return index_ == rhs.index_ && deque_ == rhs.deque_;
    }
    bool operator!=(const Iterator& rhs) const { return !(*this == rhs); }
    Iterator& operator+=(difference_type amount) {
      // `amount` might be negative, check for underflow.
      QUICHE_DCHECK_GE(static_cast<difference_type>(index_), -amount);
      index_ += amount;
      QUICHE_DCHECK_LT(index_, deque_->Size());
      return *this;
    }
    Iterator& operator-=(difference_type amount) { return operator+=(-amount); }
    difference_type operator-(const Iterator& rhs) const {
      return static_cast<difference_type>(index_) -
             static_cast<difference_type>(rhs.index_);
    }

   private:
    // |index_| is the index of the item in |*deque_|.
    std::size_t index_;
    // |deque_| is a pointer to the container the iterator came from.
    QuicIntervalDeque* deque_;

    friend class QuicIntervalDeque;
  };

  QuicIntervalDeque();

  // Adds an item to the underlying container. The |item|'s interval _should_ be
  // strictly greater than the last interval added.
  void PushBack(T&& item);
  void PushBack(const T& item);
  // Removes the front/top of the underlying container and the associated
  // interval.
  void PopFront();
  // Returns an iterator to the beginning of the data. The iterator will move
  // the |cached_index_| as the iterator moves.
  Iterator DataBegin();
  // Returns an iterator to the end of the data.
  Iterator DataEnd();
  // Returns an iterator pointing to the item in |interval_begin|. The iterator
  // will move the |cached_index_| as the iterator moves.
  Iterator DataAt(const std::size_t interval_begin);

  // Returns the number of items contained inside the structure.
  std::size_t Size() const;
  // Returns whether the structure is empty.
  bool Empty() const;

 private:
  struct QUICHE_NO_EXPORT IntervalCompare {
    bool operator()(const T& item, std::size_t interval_begin) const {
      return item.interval().max() <= interval_begin;
    }
  };

  template <class U>
  void PushBackUniversal(U&& item);

  Iterator Search(const std::size_t interval_begin,
                  const std::size_t begin_index, const std::size_t end_index);

  // For accessing the |cached_index_|
  friend class test::QuicIntervalDequePeer;

  C container_;
  std::optional<std::size_t> cached_index_;
};

template <class T, class C>
QuicIntervalDeque<T, C>::QuicIntervalDeque() {}

template <class T, class C>
void QuicIntervalDeque<T, C>::PushBack(T&& item) {
  PushBackUniversal(std::move(item));
}

template <class T, class C>
void QuicIntervalDeque<T, C>::PushBack(const T& item) {
  PushBackUniversal(item);
}

template <class T, class C>
void QuicIntervalDeque<T, C>::PopFront() {
  if (container_.size() == 0) {
    QUIC_BUG(QuicIntervalDeque_PopFront_empty)
        << "Trying to pop from an empty container.";
    return;
  }
  container_.pop_front();
  if (container_.size() == 0) {
    cached_index_.reset();
  }
  if (cached_index_.value_or(0) > 0) {
    cached_index_ = *cached_index_ - 1;
  }
}

template <class T, class C>
typename QuicIntervalDeque<T, C>::Iterator
QuicIntervalDeque<T, C>::DataBegin() {
  return Iterator(0, this);
}

template <class T, class C>
typename QuicIntervalDeque<T, C>::Iterator QuicIntervalDeque<T, C>::DataEnd() {
  return Iterator(container_.size(), this);
}

template <class T, class C>
typename QuicIntervalDeque<T, C>::Iterator QuicIntervalDeque<T, C>::DataAt(
    const std::size_t interval_begin) {
  // No |cached_index_| value means all items can be searched.
  if (!cached_index_.has_value()) {
    return Search(interval_begin, 0, container_.size());
  }

  const std::size_t cached_index = *cached_index_;
  QUICHE_DCHECK(cached_index < container_.size());

  const QuicInterval<size_t> cached_interval =
      container_[cached_index].interval();
  // Does our cached index point directly to what we want?
  if (cached_interval.Contains(interval_begin)) {
    return Iterator(cached_index, this);
  }

  // Are we off-by-one?
  const std::size_t next_index = cached_index + 1;
  if (next_index < container_.size()) {
    if (container_[next_index].interval().Contains(interval_begin)) {
      cached_index_ = next_index;
      return Iterator(next_index, this);
    }
  }

  // Small optimization:
  // Determine if we should binary search above or below the cached interval.
  const std::size_t cached_begin = cached_interval.min();
  bool looking_below = interval_begin < cached_begin;
  const std::size_t lower = looking_below ? 0 : cached_index + 1;
  const std::size_t upper = looking_below ? cached_index : container_.size();
  Iterator ret = Search(interval_begin, lower, upper);
  if (ret == DataEnd()) {
    return ret;
  }
  // Update the |cached_index_| to point to the higher index.
  if (!looking_below) {
    cached_index_ = ret.index_;
  }
  return ret;
}

template <class T, class C>
std::size_t QuicIntervalDeque<T, C>::Size() const {
  return container_.size();
}

template <class T, class C>
bool QuicIntervalDeque<T, C>::Empty() const {
  return container_.size() == 0;
}

template <class T, class C>
template <class U>
void QuicIntervalDeque<T, C>::PushBackUniversal(U&& item) {
  QuicInterval<std::size_t> interval = item.interval();
  // Adding an empty interval is a bug.
  if (interval.Empty()) {
    QUIC_BUG(QuicIntervalDeque_PushBackUniversal_empty)
        << "Trying to save empty interval to quiche::QuicheCircularDeque.";
    return;
  }
  container_.push_back(std::forward<U>(item));
  if (!cached_index_.has_value()) {
    cached_index_ = container_.size() - 1;
  }
}

template <class T, class C>
typename QuicIntervalDeque<T, C>::Iterator QuicIntervalDeque<T, C>::Search(
    const std::size_t interval_begin, const std::size_t begin_index,
    const std::size_t end_index) {
  auto begin = container_.begin() + begin_index;
  auto end = container_.begin() + end_index;
  auto res = std::lower_bound(begin, end, interval_begin, IntervalCompare());
  // Just because we run |lower_bound| and it didn't return |container_.end()|
  // doesn't mean we found our desired interval.
  if (res != end && res->interval().Contains(interval_begin)) {
    return Iterator(std::distance(begin, res) + begin_index, this);
  }
  return DataEnd();
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_INTERVAL_DEQUE_H_
