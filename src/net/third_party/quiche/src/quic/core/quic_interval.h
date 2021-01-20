// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_INTERVAL_H_
#define QUICHE_QUIC_CORE_QUIC_INTERVAL_H_

// An QuicInterval<T> is a data structure used to represent a contiguous,
// mutable range over an ordered type T. Supported operations include testing a
// value to see whether it is included in the QuicInterval, comparing two
// QuicIntervals, and performing their union, intersection, and difference. For
// the purposes of this library, an "ordered type" is any type that induces a
// total order on its values via its less-than operator (operator<()). Examples
// of such types are basic arithmetic types like int and double as well as class
// types like string.
//
// An QuicInterval<T> is represented using the usual C++ STL convention, namely
// as the half-open QuicInterval [min, max). A point p is considered to be
// contained in the QuicInterval iff p >= min && p < max. One consequence of
// this definition is that for any non-empty QuicInterval, min is contained in
// the QuicInterval but max is not. There is no canonical representation for the
// empty QuicInterval; rather, any QuicInterval where max <= min is regarded as
// empty. As a consequence, two empty QuicIntervals will still compare as equal
// despite possibly having different underlying min() or max() values. Also
// beware of the terminology used here: the library uses the terms "min" and
// "max" rather than "begin" and "end" as is conventional for the STL.
//
// T is required to be default- and copy-constructable, to have an assignment
// operator, and the full complement of comparison operators (<, <=, ==, !=, >=,
// >).  A difference operator (operator-()) is required if
// QuicInterval<T>::Length is used.
//
// QuicInterval supports operator==. Two QuicIntervals are considered equal if
// either they are both empty or if their corresponding min and max fields
// compare equal. QuicInterval also provides an operator<. Unfortunately,
// operator< is currently buggy because its behavior is inconsistent with
// operator==: two empty ranges with different representations may be regarded
// as equal by operator== but regarded as different by operator<. Bug 9240050
// has been created to address this.
//
//
// Examples:
//   QuicInterval<int> r1(0, 100);  // The QuicInterval [0, 100).
//   EXPECT_TRUE(r1.Contains(0));
//   EXPECT_TRUE(r1.Contains(50));
//   EXPECT_FALSE(r1.Contains(100));  // 100 is just outside the QuicInterval.
//
//   QuicInterval<int> r2(50, 150);  // The QuicInterval [50, 150).
//   EXPECT_TRUE(r1.Intersects(r2));
//   EXPECT_FALSE(r1.Contains(r2));
//   EXPECT_TRUE(r1.IntersectWith(r2));  // Mutates r1.
//   EXPECT_EQ(QuicInterval<int>(50, 100), r1);  // r1 is now [50, 100).
//
//   QuicInterval<int> r3(1000, 2000);  // The QuicInterval [1000, 2000).
//   EXPECT_TRUE(r1.IntersectWith(r3));  // Mutates r1.
//   EXPECT_TRUE(r1.Empty());  // Now r1 is empty.
//   EXPECT_FALSE(r1.Contains(r1.min()));  // e.g. doesn't contain its own min.

#include <stddef.h>
#include <algorithm>
#include <ostream>
#include <type_traits>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

template <typename T>
class QUIC_NO_EXPORT QuicInterval {
 private:
  // Type trait for deriving the return type for QuicInterval::Length.  If
  // operator-() is not defined for T, then the return type is void.  This makes
  // the signature for Length compile so that the class can be used for such T,
  // but code that calls Length would still generate a compilation error.
  template <typename U>
  class QUIC_NO_EXPORT DiffTypeOrVoid {
   private:
    template <typename V>
    static auto f(const V* v) -> decltype(*v - *v);
    template <typename V>
    static void f(...);

   public:
    using type = typename std::decay<decltype(f<U>(nullptr))>::type;
  };

 public:
  // Construct an QuicInterval representing an empty QuicInterval.
  QuicInterval() : min_(), max_() {}

  // Construct an QuicInterval representing the QuicInterval [min, max). If min
  // < max, the constructed object will represent the non-empty QuicInterval
  // containing all values from min up to (but not including) max. On the other
  // hand, if min >= max, the constructed object will represent the empty
  // QuicInterval.
  QuicInterval(const T& min, const T& max) : min_(min), max_(max) {}

  template <typename U1,
            typename U2,
            typename = typename std::enable_if<
                std::is_convertible<U1, T>::value &&
                std::is_convertible<U2, T>::value>::type>
  QuicInterval(U1&& min, U2&& max)
      : min_(std::forward<U1>(min)), max_(std::forward<U2>(max)) {}

  const T& min() const { return min_; }
  const T& max() const { return max_; }
  void SetMin(const T& t) { min_ = t; }
  void SetMax(const T& t) { max_ = t; }

  void Set(const T& min, const T& max) {
    SetMin(min);
    SetMax(max);
  }

  void Clear() { *this = {}; }

  bool Empty() const { return min() >= max(); }

  // Returns the length of this QuicInterval. The value returned is zero if
  // Empty() is true; otherwise the value returned is max() - min().
  typename DiffTypeOrVoid<T>::type Length() const {
    return (Empty() ? min() : max()) - min();
  }

  // Returns true iff t >= min() && t < max().
  bool Contains(const T& t) const { return min() <= t && max() > t; }

  // Returns true iff *this and i are non-empty, and *this includes i. "*this
  // includes i" means that for all t, if i.Contains(t) then this->Contains(t).
  // Note the unintuitive consequence of this definition: this method always
  // returns false when i is the empty QuicInterval.
  bool Contains(const QuicInterval& i) const {
    return !Empty() && !i.Empty() && min() <= i.min() && max() >= i.max();
  }

  // Returns true iff there exists some point t for which this->Contains(t) &&
  // i.Contains(t) evaluates to true, i.e. if the intersection is non-empty.
  bool Intersects(const QuicInterval& i) const {
    return !Empty() && !i.Empty() && min() < i.max() && max() > i.min();
  }

  // Returns true iff there exists some point t for which this->Contains(t) &&
  // i.Contains(t) evaluates to true, i.e. if the intersection is non-empty.
  // Furthermore, if the intersection is non-empty and the out pointer is not
  // null, this method stores the calculated intersection in *out.
  bool Intersects(const QuicInterval& i, QuicInterval* out) const;

  // Sets *this to be the intersection of itself with i. Returns true iff
  // *this was modified.
  bool IntersectWith(const QuicInterval& i);

  // Calculates the smallest QuicInterval containing both *this i, and updates
  // *this to represent that QuicInterval, and returns true iff *this was
  // modified.
  bool SpanningUnion(const QuicInterval& i);

  // Determines the difference between two QuicIntervals by finding all points
  // that are contained in *this but not in i, coalesces those points into the
  // largest possible contiguous QuicIntervals, and appends those QuicIntervals
  // to the *difference vector. Intuitively this can be thought of as "erasing"
  // i from *this. This will either completely erase *this (leaving nothing
  // behind), partially erase some of *this from the left or right side (leaving
  // some residual behind), or erase a hole in the middle of *this (leaving
  // behind an QuicInterval on either side). Therefore, 0, 1, or 2 QuicIntervals
  // will be appended to *difference. The method returns true iff the
  // intersection of *this and i is non-empty. The caller owns the vector and
  // the QuicInterval* pointers inside it. The difference vector is required to
  // be non-null.
  bool Difference(const QuicInterval& i,
                  std::vector<QuicInterval*>* difference) const;

  // Determines the difference between two QuicIntervals as in
  // Difference(QuicInterval&, vector*), but stores the results directly in out
  // parameters rather than dynamically allocating an QuicInterval* and
  // appending it to a vector. If two results are generated, the one with the
  // smaller value of min() will be stored in *lo and the other in *hi.
  // Otherwise (if fewer than two results are generated), unused arguments will
  // be set to the empty QuicInterval (it is possible that *lo will be empty and
  // *hi non-empty). The method returns true iff the intersection of *this and i
  // is non-empty.
  bool Difference(const QuicInterval& i,
                  QuicInterval* lo,
                  QuicInterval* hi) const;

  friend bool operator==(const QuicInterval& a, const QuicInterval& b) {
    bool ae = a.Empty();
    bool be = b.Empty();
    if (ae && be)
      return true;  // All empties are equal.
    if (ae != be)
      return false;  // Empty cannot equal nonempty.
    return a.min() == b.min() && a.max() == b.max();
  }

  friend bool operator!=(const QuicInterval& a, const QuicInterval& b) {
    return !(a == b);
  }

  // Defines a comparator which can be used to induce an order on QuicIntervals,
  // so that, for example, they can be stored in an ordered container such as
  // std::set. The ordering is arbitrary, but does provide the guarantee that,
  // for non-empty QuicIntervals X and Y, if X contains Y, then X <= Y.
  // TODO(kosak): The current implementation of this comparator has a problem
  // because the ordering it induces is inconsistent with that of Equals(). In
  // particular, this comparator does not properly consider all empty
  // QuicIntervals equivalent. Bug 9240050 has been created to track this.
  friend bool operator<(const QuicInterval& a, const QuicInterval& b) {
    return a.min() < b.min() || (!(b.min() < a.min()) && b.max() < a.max());
  }

 private:
  T min_;  // Inclusive lower bound.
  T max_;  // Exclusive upper bound.
};

// Constructs an QuicInterval by deducing the types from the function arguments.
template <typename T>
QuicInterval<T> MakeQuicInterval(T&& lhs, T&& rhs) {
  return QuicInterval<T>(std::forward<T>(lhs), std::forward<T>(rhs));
}

// Note: ideally we'd use
//   decltype(out << "[" << i.min() << ", " << i.max() << ")")
// as return type of the function, but as of July 2017 this triggers g++
// "sorry, unimplemented: string literal in function template signature" error.
template <typename T>
auto operator<<(std::ostream& out, const QuicInterval<T>& i)
    -> decltype(out << i.min()) {
  return out << "[" << i.min() << ", " << i.max() << ")";
}

//==============================================================================
// Implementation details: Clients can stop reading here.

template <typename T>
bool QuicInterval<T>::Intersects(const QuicInterval& i,
                                 QuicInterval* out) const {
  if (!Intersects(i))
    return false;
  if (out != nullptr) {
    *out = QuicInterval(std::max(min(), i.min()), std::min(max(), i.max()));
  }
  return true;
}

template <typename T>
bool QuicInterval<T>::IntersectWith(const QuicInterval& i) {
  if (Empty())
    return false;
  bool modified = false;
  if (i.min() > min()) {
    SetMin(i.min());
    modified = true;
  }
  if (i.max() < max()) {
    SetMax(i.max());
    modified = true;
  }
  return modified;
}

template <typename T>
bool QuicInterval<T>::SpanningUnion(const QuicInterval& i) {
  if (i.Empty())
    return false;
  if (Empty()) {
    *this = i;
    return true;
  }
  bool modified = false;
  if (i.min() < min()) {
    SetMin(i.min());
    modified = true;
  }
  if (i.max() > max()) {
    SetMax(i.max());
    modified = true;
  }
  return modified;
}

template <typename T>
bool QuicInterval<T>::Difference(const QuicInterval& i,
                                 std::vector<QuicInterval*>* difference) const {
  if (Empty()) {
    // <empty> - <i> = <empty>
    return false;
  }
  if (i.Empty()) {
    // <this> - <empty> = <this>
    difference->push_back(new QuicInterval(*this));
    return false;
  }
  if (min() < i.max() && min() >= i.min() && max() > i.max()) {
    //            [------ this ------)
    // [------ i ------)
    //                 [-- result ---)
    difference->push_back(new QuicInterval(i.max(), max()));
    return true;
  }
  if (max() > i.min() && max() <= i.max() && min() < i.min()) {
    // [------ this ------)
    //            [------ i ------)
    // [- result -)
    difference->push_back(new QuicInterval(min(), i.min()));
    return true;
  }
  if (min() < i.min() && max() > i.max()) {
    // [------- this --------)
    //      [---- i ----)
    // [ R1 )           [ R2 )
    // There are two results: R1 and R2.
    difference->push_back(new QuicInterval(min(), i.min()));
    difference->push_back(new QuicInterval(i.max(), max()));
    return true;
  }
  if (min() >= i.min() && max() <= i.max()) {
    //   [--- this ---)
    // [------ i --------)
    // Intersection is <this>, so difference yields the empty QuicInterval.
    // Nothing is appended to *difference.
    return true;
  }
  // No intersection. Append <this>.
  difference->push_back(new QuicInterval(*this));
  return false;
}

template <typename T>
bool QuicInterval<T>::Difference(const QuicInterval& i,
                                 QuicInterval* lo,
                                 QuicInterval* hi) const {
  // Initialize *lo and *hi to empty
  *lo = {};
  *hi = {};
  if (Empty())
    return false;
  if (i.Empty()) {
    *lo = *this;
    return false;
  }
  if (min() < i.max() && min() >= i.min() && max() > i.max()) {
    //            [------ this ------)
    // [------ i ------)
    //                 [-- result ---)
    *hi = QuicInterval(i.max(), max());
    return true;
  }
  if (max() > i.min() && max() <= i.max() && min() < i.min()) {
    // [------ this ------)
    //            [------ i ------)
    // [- result -)
    *lo = QuicInterval(min(), i.min());
    return true;
  }
  if (min() < i.min() && max() > i.max()) {
    // [------- this --------)
    //      [---- i ----)
    // [ R1 )           [ R2 )
    // There are two results: R1 and R2.
    *lo = QuicInterval(min(), i.min());
    *hi = QuicInterval(i.max(), max());
    return true;
  }
  if (min() >= i.min() && max() <= i.max()) {
    //   [--- this ---)
    // [------ i --------)
    // Intersection is <this>, so difference yields the empty QuicInterval.
    return true;
  }
  *lo = *this;  // No intersection.
  return false;
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_INTERVAL_H_
