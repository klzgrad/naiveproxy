// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_INTERVAL_SET_H_
#define QUICHE_QUIC_CORE_QUIC_INTERVAL_SET_H_

// QuicIntervalSet<T> is a data structure used to represent a sorted set of
// non-empty, non-adjacent, and mutually disjoint intervals. Mutations to an
// interval set preserve these properties, altering the set as needed. For
// example, adding [2, 3) to a set containing only [1, 2) would result in the
// set containing the single interval [1, 3).
//
// Supported operations include testing whether an Interval is contained in the
// QuicIntervalSet, comparing two QuicIntervalSets, and performing
// QuicIntervalSet union, intersection, and difference.
//
// QuicIntervalSet maintains the minimum number of entries needed to represent
// the set of underlying intervals. When the QuicIntervalSet is modified (e.g.
// due to an Add operation), other interval entries may be coalesced, removed,
// or otherwise modified in order to maintain this invariant. The intervals are
// maintained in sorted order, by ascending min() value.
//
// The reader is cautioned to beware of the terminology used here: this library
// uses the terms "min" and "max" rather than "begin" and "end" as is
// conventional for the STL. The terminology [min, max) refers to the half-open
// interval which (if the interval is not empty) contains min but does not
// contain max. An interval is considered empty if min >= max.
//
// T is required to be default- and copy-constructible, to have an assignment
// operator, a difference operator (operator-()), and the full complement of
// comparison operators (<, <=, ==, !=, >=, >). These requirements are inherited
// from value_type.
//
// QuicIntervalSet has constant-time move operations.
//
//
// Examples:
//   QuicIntervalSet<int> intervals;
//   intervals.Add(Interval<int>(10, 20));
//   intervals.Add(Interval<int>(30, 40));
//   // intervals contains [10,20) and [30,40).
//   intervals.Add(Interval<int>(15, 35));
//   // intervals has been coalesced. It now contains the single range [10,40).
//   EXPECT_EQ(1, intervals.Size());
//   EXPECT_TRUE(intervals.Contains(Interval<int>(10, 40)));
//
//   intervals.Difference(Interval<int>(10, 20));
//   // intervals should now contain the single range [20, 40).
//   EXPECT_EQ(1, intervals.Size());
//   EXPECT_TRUE(intervals.Contains(Interval<int>(20, 40)));

#include <stddef.h>

#include <algorithm>
#include <initializer_list>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "quiche/quic/core/quic_interval.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_containers.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

template <typename T>
class QUIC_NO_EXPORT QuicIntervalSet {
 public:
  using value_type = QuicInterval<T>;

 private:
  struct QUIC_NO_EXPORT IntervalLess {
    using is_transparent = void;
    bool operator()(const value_type& a, const value_type& b) const;
    // These transparent overloads are used when we do all of our searches (via
    // Set::lower_bound() and Set::upper_bound()), which avoids the need to
    // construct an interval when we are looking for a point and also avoids
    // needing to worry about comparing overlapping intervals in the overload
    // that takes two value_types (the one just above this comment).
    bool operator()(const value_type& a, const T& point) const;
    bool operator()(const value_type& a, T&& point) const;
    bool operator()(const T& point, const value_type& a) const;
    bool operator()(T&& point, const value_type& a) const;
  };

  using Set = quiche::QuicheSmallOrderedSet<value_type, IntervalLess>;

 public:
  using const_iterator = typename Set::const_iterator;
  using const_reverse_iterator = typename Set::const_reverse_iterator;

  // Instantiates an empty QuicIntervalSet.
  QuicIntervalSet() = default;

  // Instantiates a QuicIntervalSet containing exactly one initial half-open
  // interval [min, max), unless the given interval is empty, in which case the
  // QuicIntervalSet will be empty.
  explicit QuicIntervalSet(const value_type& interval) { Add(interval); }

  // Instantiates a QuicIntervalSet containing the half-open interval [min,
  // max).
  QuicIntervalSet(const T& min, const T& max) { Add(min, max); }

  QuicIntervalSet(std::initializer_list<value_type> il) { assign(il); }

  // Clears this QuicIntervalSet.
  void Clear() { intervals_.clear(); }

  // Returns the number of disjoint intervals contained in this QuicIntervalSet.
  size_t Size() const { return intervals_.size(); }

  // Returns the smallest interval that contains all intervals in this
  // QuicIntervalSet, or the empty interval if the set is empty.
  value_type SpanningInterval() const;

  // Adds "interval" to this QuicIntervalSet. Adding the empty interval has no
  // effect.
  void Add(const value_type& interval);

  // Adds the interval [min, max) to this QuicIntervalSet. Adding the empty
  // interval has no effect.
  void Add(const T& min, const T& max) { Add(value_type(min, max)); }

  // Same semantics as Add(const value_type&), but optimized for the case where
  // rbegin()->min() <= |interval|.min() <= rbegin()->max().
  void AddOptimizedForAppend(const value_type& interval) {
    if (Empty() || !GetQuicFlag(quic_interval_set_enable_add_optimization)) {
      Add(interval);
      return;
    }

    const_reverse_iterator last_interval = intervals_.rbegin();

    // If interval.min() is outside of [last_interval->min, last_interval->max],
    // we can not simply extend last_interval->max.
    if (interval.min() < last_interval->min() ||
        interval.min() > last_interval->max()) {
      Add(interval);
      return;
    }

    if (interval.max() <= last_interval->max()) {
      // interval is fully contained by last_interval.
      return;
    }

    // Extend last_interval.max to interval.max, in place.
    //
    // Set does not allow in-place updates due to the potential of violating its
    // ordering requirements. But we know setting the max of the last interval
    // is safe w.r.t set ordering and other invariants of QuicIntervalSet, so we
    // force an in-place update for performance.
    const_cast<value_type*>(&(*last_interval))->SetMax(interval.max());
  }

  // Same semantics as Add(const T&, const T&), but optimized for the case where
  // rbegin()->max() == |min|.
  void AddOptimizedForAppend(const T& min, const T& max) {
    AddOptimizedForAppend(value_type(min, max));
  }

  // TODO(wub): Similar to AddOptimizedForAppend, we can also have a
  // AddOptimizedForPrepend if there is a use case.

  // Remove the first interval.
  // REQUIRES: !Empty()
  void PopFront() {
    QUICHE_DCHECK(!Empty());
    intervals_.erase(intervals_.begin());
  }

  // Trim all values that are smaller than |value|. Which means
  // a) If all values in an interval is smaller than |value|, the entire
  //    interval is removed.
  // b) If some but not all values in an interval is smaller than |value|, the
  //    min of that interval is raised to |value|.
  // Returns true if some intervals are trimmed.
  bool TrimLessThan(const T& value) {
    // Number of intervals that are fully or partially trimmed.
    size_t num_intervals_trimmed = 0;

    while (!intervals_.empty()) {
      const_iterator first_interval = intervals_.begin();
      if (first_interval->min() >= value) {
        break;
      }

      ++num_intervals_trimmed;

      if (first_interval->max() <= value) {
        // a) Trim the entire interval.
        intervals_.erase(first_interval);
        continue;
      }

      // b) Trim a prefix of the interval.
      //
      // Set does not allow in-place updates due to the potential of violating
      // its ordering requirements. But increasing the min of the first interval
      // will not break the ordering, hence the const_cast.
      const_cast<value_type*>(&(*first_interval))->SetMin(value);
      break;
    }

    return num_intervals_trimmed != 0;
  }

  // Returns true if this QuicIntervalSet is empty.
  bool Empty() const { return intervals_.empty(); }

  // Returns true if any interval in this QuicIntervalSet contains the indicated
  // value.
  bool Contains(const T& value) const;

  // Returns true if there is some interval in this QuicIntervalSet that wholly
  // contains the given interval. An interval O "wholly contains" a non-empty
  // interval I if O.Contains(p) is true for every p in I. This is the same
  // definition used by value_type::Contains(). This method returns false on
  // the empty interval, due to a (perhaps unintuitive) convention inherited
  // from value_type.
  // Example:
  //   Assume an QuicIntervalSet containing the entries { [10,20), [30,40) }.
  //   Contains(Interval(15, 16)) returns true, because [10,20) contains
  //   [15,16). However, Contains(Interval(15, 35)) returns false.
  bool Contains(const value_type& interval) const;

  // Returns true if for each interval in "other", there is some (possibly
  // different) interval in this QuicIntervalSet which wholly contains it. See
  // Contains(const value_type& interval) for the meaning of "wholly contains".
  // Perhaps unintuitively, this method returns false if "other" is the empty
  // set. The algorithmic complexity of this method is O(other.Size() *
  // log(this->Size())). The method could be rewritten to run in O(other.Size()
  // + this->Size()), and this alternative could be implemented as a free
  // function using the public API.
  bool Contains(const QuicIntervalSet<T>& other) const;

  // Returns true if there is some interval in this QuicIntervalSet that wholly
  // contains the interval [min, max). See Contains(const value_type&).
  bool Contains(const T& min, const T& max) const {
    return Contains(value_type(min, max));
  }

  // Returns true if for some interval in "other", there is some interval in
  // this QuicIntervalSet that intersects with it. See value_type::Intersects()
  // for the definition of interval intersection.  Runs in time O(n+m) where n
  // is the number of intervals in this and m is the number of intervals in
  // other.
  bool Intersects(const QuicIntervalSet& other) const;

  // Returns an iterator to the value_type in the QuicIntervalSet that contains
  // the given value. In other words, returns an iterator to the unique interval
  // [min, max) in the QuicIntervalSet that has the property min <= value < max.
  // If there is no such interval, this method returns end().
  const_iterator Find(const T& value) const;

  // Returns an iterator to the value_type in the QuicIntervalSet that wholly
  // contains the given interval. In other words, returns an iterator to the
  // unique interval outer in the QuicIntervalSet that has the property that
  // outer.Contains(interval). If there is no such interval, or if interval is
  // empty, returns end().
  const_iterator Find(const value_type& interval) const;

  // Returns an iterator to the value_type in the QuicIntervalSet that wholly
  // contains [min, max). In other words, returns an iterator to the unique
  // interval outer in the QuicIntervalSet that has the property that
  // outer.Contains(Interval<T>(min, max)). If there is no such interval, or if
  // interval is empty, returns end().
  const_iterator Find(const T& min, const T& max) const {
    return Find(value_type(min, max));
  }

  // Returns an iterator pointing to the first value_type which contains or
  // goes after the given value.
  //
  // Example:
  //   [10, 20)  [30, 40)
  //   ^                    LowerBound(10)
  //   ^                    LowerBound(15)
  //             ^          LowerBound(20)
  //             ^          LowerBound(25)
  const_iterator LowerBound(const T& value) const;

  // Returns an iterator pointing to the first value_type which goes after
  // the given value.
  //
  // Example:
  //   [10, 20)  [30, 40)
  //             ^          UpperBound(10)
  //             ^          UpperBound(15)
  //             ^          UpperBound(20)
  //             ^          UpperBound(25)
  const_iterator UpperBound(const T& value) const;

  // Returns true if every value within the passed interval is not Contained
  // within the QuicIntervalSet.
  // Note that empty intervals are always considered disjoint from the
  // QuicIntervalSet (even though the QuicIntervalSet doesn't `Contain` them).
  bool IsDisjoint(const value_type& interval) const;

  // Merges all the values contained in "other" into this QuicIntervalSet.
  //
  // Performance: Let n == Size() and m = other.Size().  Union() runs in O(m)
  // Set operations, so that if Set is a tree, it runs in time O(m log(n+m)) and
  // if Set is a flat_set it runs in time O(m(n+m)).  In principle, for the
  // flat_set, we should be able to make this run in time O(n+m).
  //
  // TODO(bradleybear): Make Union() run in time O(n+m) for flat_set.  This may
  // require an additional template parameter to indicate that the Set is a
  // linear-time data structure instead of a log-time data structure.
  void Union(const QuicIntervalSet& other);

  // Modifies this QuicIntervalSet so that it contains only those values that
  // are currently present both in *this and in the QuicIntervalSet "other".
  void Intersection(const QuicIntervalSet& other);

  // Mutates this QuicIntervalSet so that it contains only those values that are
  // currently in *this but not in "interval".
  void Difference(const value_type& interval);

  // Mutates this QuicIntervalSet so that it contains only those values that are
  // currently in *this but not in the interval [min, max).
  void Difference(const T& min, const T& max);

  // Mutates this QuicIntervalSet so that it contains only those values that are
  // currently in *this but not in the QuicIntervalSet "other".  Runs in time
  // O(n+m) where n is this->Size(), m is other.Size(), regardless of whether
  // the Set is a flat_set or a std::set.
  void Difference(const QuicIntervalSet& other);

  // Mutates this QuicIntervalSet so that it contains only those values that are
  // in [min, max) but not currently in *this.
  void Complement(const T& min, const T& max);

  // QuicIntervalSet's begin() iterator. The invariants of QuicIntervalSet
  // guarantee that for each entry e in the set, e.min() < e.max() (because the
  // entries are non-empty) and for each entry f that appears later in the set,
  // e.max() < f.min() (because the entries are ordered, pairwise-disjoint, and
  // non-adjacent). Modifications to this QuicIntervalSet invalidate these
  // iterators.
  const_iterator begin() const { return intervals_.begin(); }

  // QuicIntervalSet's end() iterator.
  const_iterator end() const { return intervals_.end(); }

  // QuicIntervalSet's rbegin() and rend() iterators. Iterator invalidation
  // semantics are the same as those for begin() / end().
  const_reverse_iterator rbegin() const { return intervals_.rbegin(); }

  const_reverse_iterator rend() const { return intervals_.rend(); }

  template <typename Iter>
  void assign(Iter first, Iter last) {
    Clear();
    for (; first != last; ++first) Add(*first);
  }

  void assign(std::initializer_list<value_type> il) {
    assign(il.begin(), il.end());
  }

  // Returns a human-readable representation of this set. This will typically be
  // (though is not guaranteed to be) of the form
  //   "[a1, b1) [a2, b2) ... [an, bn)"
  // where the intervals are in the same order as given by traversal from
  // begin() to end(). This representation is intended for human consumption;
  // computer programs should not rely on the output being in exactly this form.
  std::string ToString() const;

  QuicIntervalSet& operator=(std::initializer_list<value_type> il) {
    assign(il.begin(), il.end());
    return *this;
  }

  friend bool operator==(const QuicIntervalSet& a, const QuicIntervalSet& b) {
    return a.Size() == b.Size() &&
           std::equal(a.begin(), a.end(), b.begin(), NonemptyIntervalEq());
  }

  friend bool operator!=(const QuicIntervalSet& a, const QuicIntervalSet& b) {
    return !(a == b);
  }

 private:
  // Simple member-wise equality, since all intervals are non-empty.
  struct QUIC_NO_EXPORT NonemptyIntervalEq {
    bool operator()(const value_type& a, const value_type& b) const {
      return a.min() == b.min() && a.max() == b.max();
    }
  };

  // Returns true if this set is valid (i.e. all intervals in it are non-empty,
  // non-adjacent, and mutually disjoint). Currently this is used as an
  // integrity check by the Intersection() and Difference() methods, but is only
  // invoked for debug builds (via QUICHE_DCHECK).
  bool Valid() const;

  // Finds the first interval that potentially intersects 'other'.
  const_iterator FindIntersectionCandidate(const QuicIntervalSet& other) const;

  // Finds the first interval that potentially intersects 'interval'.  More
  // precisely, return an interator it pointing at the last interval J such that
  // interval <= J.  If all the intervals are > J then return begin().
  const_iterator FindIntersectionCandidate(const value_type& interval) const;

  // Helper for Intersection() and Difference(): Finds the next pair of
  // intervals from 'x' and 'y' that intersect. 'mine' is an iterator
  // over x->intervals_. 'theirs' is an iterator over y.intervals_. 'mine'
  // and 'theirs' are advanced until an intersecting pair is found.
  // Non-intersecting intervals (aka "holes") from x->intervals_ can be
  // optionally erased by "on_hole". "on_hole" must return an iterator to the
  // first element in 'x' after the hole, or x->intervals_.end() if no elements
  // exist after the hole.
  template <typename X, typename Func>
  static bool FindNextIntersectingPairImpl(X* x, const QuicIntervalSet& y,
                                           const_iterator* mine,
                                           const_iterator* theirs,
                                           Func on_hole);

  // The variant of the above method that doesn't mutate this QuicIntervalSet.
  bool FindNextIntersectingPair(const QuicIntervalSet& other,
                                const_iterator* mine,
                                const_iterator* theirs) const {
    return FindNextIntersectingPairImpl(
        this, other, mine, theirs,
        [](const QuicIntervalSet*, const_iterator, const_iterator end) {
          return end;
        });
  }

  // The variant of the above method that mutates this QuicIntervalSet by
  // erasing holes.
  bool FindNextIntersectingPairAndEraseHoles(const QuicIntervalSet& other,
                                             const_iterator* mine,
                                             const_iterator* theirs) {
    return FindNextIntersectingPairImpl(
        this, other, mine, theirs,
        [](QuicIntervalSet* x, const_iterator from, const_iterator to) {
          return x->intervals_.erase(from, to);
        });
  }

  // The representation for the intervals. The intervals in this set are
  // non-empty, pairwise-disjoint, non-adjacent and ordered in ascending order
  // by min().
  Set intervals_;
};

template <typename T>
auto operator<<(std::ostream& out, const QuicIntervalSet<T>& seq)
    -> decltype(out << *seq.begin()) {
  out << "{";
  for (const auto& interval : seq) {
    out << " " << interval;
  }
  out << " }";

  return out;
}

//==============================================================================
// Implementation details: Clients can stop reading here.

template <typename T>
typename QuicIntervalSet<T>::value_type QuicIntervalSet<T>::SpanningInterval()
    const {
  value_type result;
  if (!intervals_.empty()) {
    result.SetMin(intervals_.begin()->min());
    result.SetMax(intervals_.rbegin()->max());
  }
  return result;
}

template <typename T>
void QuicIntervalSet<T>::Add(const value_type& interval) {
  if (interval.Empty()) return;
  const_iterator it = intervals_.lower_bound(interval.min());
  value_type the_union = interval;
  if (it != intervals_.begin()) {
    --it;
    if (it->Separated(the_union)) {
      ++it;
    }
  }
  // Don't erase the elements one at a time, since that will produce quadratic
  // work on a flat_set, and apparently an extra log-factor of work for a
  // tree-based set.  Instead identify the first and last intervals that need to
  // be erased, and call erase only once.
  const_iterator start = it;
  while (it != intervals_.end() && !it->Separated(the_union)) {
    the_union.SpanningUnion(*it);
    ++it;
  }
  intervals_.erase(start, it);
  intervals_.insert(the_union);
}

template <typename T>
bool QuicIntervalSet<T>::Contains(const T& value) const {
  // Find the first interval with min() > value, then move back one step
  const_iterator it = intervals_.upper_bound(value);
  if (it == intervals_.begin()) return false;
  --it;
  return it->Contains(value);
}

template <typename T>
bool QuicIntervalSet<T>::Contains(const value_type& interval) const {
  // Find the first interval with min() > value, then move back one step.
  const_iterator it = intervals_.upper_bound(interval.min());
  if (it == intervals_.begin()) return false;
  --it;
  return it->Contains(interval);
}

template <typename T>
bool QuicIntervalSet<T>::Contains(const QuicIntervalSet<T>& other) const {
  if (!SpanningInterval().Contains(other.SpanningInterval())) {
    return false;
  }

  for (const_iterator i = other.begin(); i != other.end(); ++i) {
    // If we don't contain the interval, can return false now.
    if (!Contains(*i)) {
      return false;
    }
  }
  return true;
}

// This method finds the interval that Contains() "value", if such an interval
// exists in the QuicIntervalSet. The way this is done is to locate the
// "candidate interval", the only interval that could *possibly* contain value,
// and test it using Contains(). The candidate interval is the interval with the
// largest min() having min() <= value.
//
// Another detail involves the choice of which Set method to use to try to find
// the candidate interval. The most appropriate entry point is
// Set::upper_bound(), which finds the least interval with a min > the
// value. The semantics of upper_bound() are slightly different from what we
// want (namely, to find the greatest interval which is <= the probe interval)
// but they are close enough; the interval found by upper_bound() will always be
// one step past the interval we are looking for (if it exists) or at begin()
// (if it does not). Getting to the proper interval is a simple matter of
// decrementing the iterator.
template <typename T>
typename QuicIntervalSet<T>::const_iterator QuicIntervalSet<T>::Find(
    const T& value) const {
  const_iterator it = intervals_.upper_bound(value);
  if (it == intervals_.begin()) return intervals_.end();
  --it;
  if (it->Contains(value))
    return it;
  else
    return intervals_.end();
}

// This method finds the interval that Contains() the interval "probe", if such
// an interval exists in the QuicIntervalSet. The way this is done is to locate
// the "candidate interval", the only interval that could *possibly* contain
// "probe", and test it using Contains().  We use the same algorithm as for
// Find(value), except that instead of checking that the value is contained, we
// check that the probe is contained.
template <typename T>
typename QuicIntervalSet<T>::const_iterator QuicIntervalSet<T>::Find(
    const value_type& probe) const {
  const_iterator it = intervals_.upper_bound(probe.min());
  if (it == intervals_.begin()) return intervals_.end();
  --it;
  if (it->Contains(probe))
    return it;
  else
    return intervals_.end();
}

template <typename T>
typename QuicIntervalSet<T>::const_iterator QuicIntervalSet<T>::LowerBound(
    const T& value) const {
  const_iterator it = intervals_.lower_bound(value);
  if (it == intervals_.begin()) {
    return it;
  }

  // The previous intervals_.lower_bound() checking is essentially based on
  // interval.min(), so we need to check whether the `value` is contained in
  // the previous interval.
  --it;
  if (it->Contains(value)) {
    return it;
  } else {
    return ++it;
  }
}

template <typename T>
typename QuicIntervalSet<T>::const_iterator QuicIntervalSet<T>::UpperBound(
    const T& value) const {
  return intervals_.upper_bound(value);
}

template <typename T>
bool QuicIntervalSet<T>::IsDisjoint(const value_type& interval) const {
  if (interval.Empty()) return true;
  // Find the first interval with min() > interval.min()
  const_iterator it = intervals_.upper_bound(interval.min());
  if (it != intervals_.end() && interval.max() > it->min()) return false;
  if (it == intervals_.begin()) return true;
  --it;
  return it->max() <= interval.min();
}

template <typename T>
void QuicIntervalSet<T>::Union(const QuicIntervalSet& other) {
  for (const value_type& interval : other.intervals_) {
    Add(interval);
  }
}

template <typename T>
typename QuicIntervalSet<T>::const_iterator
QuicIntervalSet<T>::FindIntersectionCandidate(
    const QuicIntervalSet& other) const {
  return FindIntersectionCandidate(*other.intervals_.begin());
}

template <typename T>
typename QuicIntervalSet<T>::const_iterator
QuicIntervalSet<T>::FindIntersectionCandidate(
    const value_type& interval) const {
  // Use upper_bound to efficiently find the first interval in intervals_
  // where min() is greater than interval.min().  If the result
  // isn't the beginning of intervals_ then move backwards one interval since
  // the interval before it is the first candidate where max() may be
  // greater than interval.min().
  // In other words, no interval before that can possibly intersect with any
  // of other.intervals_.
  const_iterator mine = intervals_.upper_bound(interval.min());
  if (mine != intervals_.begin()) {
    --mine;
  }
  return mine;
}

template <typename T>
template <typename X, typename Func>
bool QuicIntervalSet<T>::FindNextIntersectingPairImpl(X* x,
                                                      const QuicIntervalSet& y,
                                                      const_iterator* mine,
                                                      const_iterator* theirs,
                                                      Func on_hole) {
  QUICHE_CHECK(x != nullptr);
  if ((*mine == x->intervals_.end()) || (*theirs == y.intervals_.end())) {
    return false;
  }
  while (!(**mine).Intersects(**theirs)) {
    const_iterator erase_first = *mine;
    // Skip over intervals in 'mine' that don't reach 'theirs'.
    while (*mine != x->intervals_.end() && (**mine).max() <= (**theirs).min()) {
      ++(*mine);
    }
    *mine = on_hole(x, erase_first, *mine);
    // We're done if the end of intervals_ is reached.
    if (*mine == x->intervals_.end()) {
      return false;
    }
    // Skip over intervals 'theirs' that don't reach 'mine'.
    while (*theirs != y.intervals_.end() &&
           (**theirs).max() <= (**mine).min()) {
      ++(*theirs);
    }
    // If the end of other.intervals_ is reached, we're done.
    if (*theirs == y.intervals_.end()) {
      on_hole(x, *mine, x->intervals_.end());
      return false;
    }
  }
  return true;
}

template <typename T>
void QuicIntervalSet<T>::Intersection(const QuicIntervalSet& other) {
  if (!SpanningInterval().Intersects(other.SpanningInterval())) {
    intervals_.clear();
    return;
  }

  const_iterator mine = FindIntersectionCandidate(other);
  // Remove any intervals that cannot possibly intersect with other.intervals_.
  mine = intervals_.erase(intervals_.begin(), mine);
  const_iterator theirs = other.FindIntersectionCandidate(*this);

  while (FindNextIntersectingPairAndEraseHoles(other, &mine, &theirs)) {
    // OK, *mine and *theirs intersect.  Now, we find the largest
    // span of intervals in other (starting at theirs) - say [a..b]
    // - that intersect *mine, and we replace *mine with (*mine
    // intersect x) for all x in [a..b] Note that subsequent
    // intervals in this can't intersect any intervals in [a..b) --
    // they may only intersect b or subsequent intervals in other.
    value_type i(*mine);
    intervals_.erase(mine);
    mine = intervals_.end();
    value_type intersection;
    while (theirs != other.intervals_.end() &&
           i.Intersects(*theirs, &intersection)) {
      std::pair<const_iterator, bool> ins = intervals_.insert(intersection);
      QUICHE_DCHECK(ins.second);
      mine = ins.first;
      ++theirs;
    }
    QUICHE_DCHECK(mine != intervals_.end());
    --theirs;
    ++mine;
  }
  QUICHE_DCHECK(Valid());
}

template <typename T>
bool QuicIntervalSet<T>::Intersects(const QuicIntervalSet& other) const {
  // Don't bother to handle nonoverlapping spanning intervals as a special case.
  // This code runs in time O(n+m), as guaranteed, even for that case .
  // Handling the nonoverlapping spanning intervals as a special case doesn't
  // improve the asymptotics but does make the code more complex.
  auto mine = intervals_.begin();
  auto theirs = other.intervals_.begin();
  while (mine != intervals_.end() && theirs != other.intervals_.end()) {
    if (mine->Intersects(*theirs))
      return true;
    else if (*mine < *theirs)
      ++mine;
    else
      ++theirs;
  }
  return false;
}

template <typename T>
void QuicIntervalSet<T>::Difference(const value_type& interval) {
  if (!SpanningInterval().Intersects(interval)) {
    return;
  }
  Difference(QuicIntervalSet<T>(interval));
}

template <typename T>
void QuicIntervalSet<T>::Difference(const T& min, const T& max) {
  Difference(value_type(min, max));
}

template <typename T>
void QuicIntervalSet<T>::Difference(const QuicIntervalSet& other) {
  // In order to avoid quadratic-time when using a flat set, we don't try to
  // update intervals_ in place.  Instead we build up a new result_, always
  // inserting at the end which is O(1) time per insertion.  Since the number of
  // elements in the result is O(Size() + other.Size()), the cost for all the
  // insertions is also O(Size() + other.Size()).
  //
  // We look at all the elements of intervals_, so that's O(Size()).
  //
  // We also look at all the elements of other.intervals_, for O(other.Size()).
  if (Empty()) return;
  Set result;
  const_iterator mine = intervals_.begin();
  value_type myinterval = *mine;
  const_iterator theirs = other.intervals_.begin();
  while (mine != intervals_.end()) {
    // Loop invariants:
    //   myinterval is nonempty.
    //   mine points at a range that is a suffix of myinterval.
    QUICHE_DCHECK(!myinterval.Empty());
    QUICHE_DCHECK(myinterval.max() == mine->max());

    // There are 3 cases.
    //  myinterval is completely before theirs (treat theirs==end() as if it is
    //  infinity).
    //    --> consume myinterval into result.
    //  myinterval is completely after theirs
    //    --> theirs can no longer affect us, so ++theirs.
    //  myinterval touches theirs with a prefix of myinterval not touching
    //  *theirs.
    //    --> consume the prefix of myinterval into the result.
    //  myinterval touches theirs, with the first element of myinterval in
    //  *theirs.
    //    -> reduce myinterval
    if (theirs == other.intervals_.end() || myinterval.max() <= theirs->min()) {
      // Keep all of my_interval.
      result.insert(result.end(), myinterval);
      myinterval.Clear();
    } else if (theirs->max() <= myinterval.min()) {
      ++theirs;
    } else if (myinterval.min() < theirs->min()) {
      // Keep a nonempty prefix of my interval.
      result.insert(result.end(), value_type(myinterval.min(), theirs->min()));
      myinterval.SetMin(theirs->max());
    } else {
      // myinterval starts at or after *theirs, chop down myinterval.
      myinterval.SetMin(theirs->max());
    }
    // if myinterval became empty, find the next interval
    if (myinterval.Empty()) {
      ++mine;
      if (mine != intervals_.end()) {
        myinterval = *mine;
      }
    }
  }
  std::swap(result, intervals_);
  QUICHE_DCHECK(Valid());
}

template <typename T>
void QuicIntervalSet<T>::Complement(const T& min, const T& max) {
  QuicIntervalSet<T> span(min, max);
  span.Difference(*this);
  intervals_.swap(span.intervals_);
}

template <typename T>
std::string QuicIntervalSet<T>::ToString() const {
  std::ostringstream os;
  os << *this;
  return os.str();
}

template <typename T>
bool QuicIntervalSet<T>::Valid() const {
  const_iterator prev = end();
  for (const_iterator it = begin(); it != end(); ++it) {
    // invalid or empty interval.
    if (it->min() >= it->max()) return false;
    // Not sorted, not disjoint, or adjacent.
    if (prev != end() && prev->max() >= it->min()) return false;
    prev = it;
  }
  return true;
}

// This comparator orders intervals first by ascending min().  The Set never
// contains overlapping intervals, so that suffices.
template <typename T>
bool QuicIntervalSet<T>::IntervalLess::operator()(const value_type& a,
                                                  const value_type& b) const {
  // This overload is probably used only by Set::insert().
  return a.min() < b.min();
}

// It appears that the Set::lower_bound(T) method uses only two overloads of the
// comparison operator that take a T as the second argument..  In contrast
// Set::upper_bound(T) uses the two overloads that take T as the first argument.
template <typename T>
bool QuicIntervalSet<T>::IntervalLess::operator()(const value_type& a,
                                                  const T& point) const {
  // Compare an interval to a point.
  return a.min() < point;
}

template <typename T>
bool QuicIntervalSet<T>::IntervalLess::operator()(const value_type& a,
                                                  T&& point) const {
  // Compare an interval to a point
  return a.min() < point;
}

// It appears that the Set::upper_bound(T) method uses only the next two
// overloads of the comparison operator.
template <typename T>
bool QuicIntervalSet<T>::IntervalLess::operator()(const T& point,
                                                  const value_type& a) const {
  // Compare an interval to a point.
  return point < a.min();
}

template <typename T>
bool QuicIntervalSet<T>::IntervalLess::operator()(T&& point,
                                                  const value_type& a) const {
  // Compare an interval to a point.
  return point < a.min();
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_INTERVAL_SET_H_
