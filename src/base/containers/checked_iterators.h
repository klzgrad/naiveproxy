// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CONTAINERS_CHECKED_ITERATORS_H_
#define BASE_CONTAINERS_CHECKED_ITERATORS_H_

#include <iterator>
#include <memory>
#include <type_traits>

#include "base/containers/util.h"
#include "base/logging.h"

namespace base {

template <typename T>
class CheckedContiguousIterator {
 public:
  using difference_type = std::ptrdiff_t;
  using value_type = std::remove_cv_t<T>;
  using pointer = T*;
  using reference = T&;
  using iterator_category = std::random_access_iterator_tag;

  // Required for converting constructor below.
  template <typename U>
  friend class CheckedContiguousIterator;

  constexpr CheckedContiguousIterator() = default;

#if defined(_LIBCPP_VERSION)
  // The following using declaration, single argument implicit constructor and
  // friended `__unwrap_iter` overload are required to use an optimized code
  // path when using a CheckedContiguousIterator with libc++ algorithms such as
  // std::copy(first, last, result), std::copy_backward(first, last, result),
  // std::move(first, last, result) and std::move_backward(first, last, result).
  //
  // Each of these algorithms dispatches to a std::memmove if this is safe to do
  // so, i.e. when all of `first`, `last` and `result` are iterators over
  // contiguous storage of the same type modulo const qualifiers.
  //
  // libc++ implements this for its contiguous iterators by invoking the
  // unqualified __unwrap_iter, which returns the underlying pointer for
  // iterators over std::vector and std::string, and returns the original
  // iterator otherwise.
  //
  // Thus in order to opt into this optimization for CCI, we need to provide our
  // own __unwrap_iter, returning the underlying raw pointer if it is safe to do
  // so.
  //
  // Furthermore, considering that std::copy is implemented as follows, the
  // return type of __unwrap_iter(CCI) needs to be convertible to CCI, which is
  // why an appropriate implicit single argument constructor is provided for the
  // optimized case:
  //
  //     template <class InIter, class OutIter>
  //     OutIter copy(InIter first, InIter last, OutIter result) {
  //       return __copy(__unwrap_iter(first), __unwrap_iter(last),
  //                     __unwrap_iter(result));
  //     }
  //
  //     Unoptimized __copy() signature:
  //     template <class InIter, class OutIter>
  //     OutIter __copy(InIter first, InIter last, OutIter result);
  //
  //     Optimized __copy() signature:
  //     template <class T, class U>
  //     U* __copy(T* first, T* last, U* result);
  //
  // Finally, this single argument constructor sets all internal fields to the
  // passed in pointer. This allows the resulting CCI to be used in other
  // optimized calls to std::copy (or std::move, std::copy_backward,
  // std::move_backward). However, it should not be used otherwise, since
  // invoking any of its public API will result in a CHECK failure. This also
  // means that callers should never use the single argument constructor
  // directly.
  template <typename U>
  using PtrIfSafeToMemmove = std::enable_if_t<
      std::is_trivially_copy_assignable<std::remove_const_t<U>>::value,
      U*>;

  template <int&... ExplicitArgumentBarrier, typename U = T>
  constexpr CheckedContiguousIterator(PtrIfSafeToMemmove<U> ptr)
      : start_(ptr), current_(ptr), end_(ptr) {}

  template <int&... ExplicitArgumentBarrier, typename U = T>
  friend constexpr PtrIfSafeToMemmove<U> __unwrap_iter(
      CheckedContiguousIterator iter) {
    return iter.current_;
  }
#endif

  constexpr CheckedContiguousIterator(T* start, const T* end)
      : CheckedContiguousIterator(start, start, end) {}
  constexpr CheckedContiguousIterator(const T* start, T* current, const T* end)
      : start_(start), current_(current), end_(end) {
    CHECK_LE(start, current);
    CHECK_LE(current, end);
  }
  constexpr CheckedContiguousIterator(const CheckedContiguousIterator& other) =
      default;

  // Converting constructor allowing conversions like CCI<T> to CCI<const T>,
  // but disallowing CCI<const T> to CCI<T> or CCI<Derived> to CCI<Base>, which
  // are unsafe. Furthermore, this is the same condition as used by the
  // converting constructors of std::span<T> and std::unique_ptr<T[]>.
  // See https://wg21.link/n4042 for details.
  template <
      typename U,
      std::enable_if_t<std::is_convertible<U (*)[], T (*)[]>::value>* = nullptr>
  constexpr CheckedContiguousIterator(const CheckedContiguousIterator<U>& other)
      : start_(other.start_), current_(other.current_), end_(other.end_) {
    // We explicitly don't delegate to the 3-argument constructor here. Its
    // CHECKs would be redundant, since we expect |other| to maintain its own
    // invariant. However, DCHECKs never hurt anybody. Presumably.
    DCHECK_LE(other.start_, other.current_);
    DCHECK_LE(other.current_, other.end_);
  }

  ~CheckedContiguousIterator() = default;

  constexpr CheckedContiguousIterator& operator=(
      const CheckedContiguousIterator& other) = default;

  friend constexpr bool operator==(const CheckedContiguousIterator& lhs,
                                   const CheckedContiguousIterator& rhs) {
    lhs.CheckComparable(rhs);
    return lhs.current_ == rhs.current_;
  }

  friend constexpr bool operator!=(const CheckedContiguousIterator& lhs,
                                   const CheckedContiguousIterator& rhs) {
    lhs.CheckComparable(rhs);
    return lhs.current_ != rhs.current_;
  }

  friend constexpr bool operator<(const CheckedContiguousIterator& lhs,
                                  const CheckedContiguousIterator& rhs) {
    lhs.CheckComparable(rhs);
    return lhs.current_ < rhs.current_;
  }

  friend constexpr bool operator<=(const CheckedContiguousIterator& lhs,
                                   const CheckedContiguousIterator& rhs) {
    lhs.CheckComparable(rhs);
    return lhs.current_ <= rhs.current_;
  }
  friend constexpr bool operator>(const CheckedContiguousIterator& lhs,
                                  const CheckedContiguousIterator& rhs) {
    lhs.CheckComparable(rhs);
    return lhs.current_ > rhs.current_;
  }

  friend constexpr bool operator>=(const CheckedContiguousIterator& lhs,
                                   const CheckedContiguousIterator& rhs) {
    lhs.CheckComparable(rhs);
    return lhs.current_ >= rhs.current_;
  }

  constexpr CheckedContiguousIterator& operator++() {
    CHECK_NE(current_, end_);
    ++current_;
    return *this;
  }

  constexpr CheckedContiguousIterator operator++(int) {
    CheckedContiguousIterator old = *this;
    ++*this;
    return old;
  }

  constexpr CheckedContiguousIterator& operator--() {
    CHECK_NE(current_, start_);
    --current_;
    return *this;
  }

  constexpr CheckedContiguousIterator operator--(int) {
    CheckedContiguousIterator old = *this;
    --*this;
    return old;
  }

  constexpr CheckedContiguousIterator& operator+=(difference_type rhs) {
    if (rhs > 0) {
      CHECK_LE(rhs, end_ - current_);
    } else {
      CHECK_LE(-rhs, current_ - start_);
    }
    current_ += rhs;
    return *this;
  }

  constexpr CheckedContiguousIterator operator+(difference_type rhs) const {
    CheckedContiguousIterator it = *this;
    it += rhs;
    return it;
  }

  constexpr CheckedContiguousIterator& operator-=(difference_type rhs) {
    if (rhs < 0) {
      CHECK_LE(-rhs, end_ - current_);
    } else {
      CHECK_LE(rhs, current_ - start_);
    }
    current_ -= rhs;
    return *this;
  }

  constexpr CheckedContiguousIterator operator-(difference_type rhs) const {
    CheckedContiguousIterator it = *this;
    it -= rhs;
    return it;
  }

  constexpr friend difference_type operator-(
      const CheckedContiguousIterator& lhs,
      const CheckedContiguousIterator& rhs) {
    lhs.CheckComparable(rhs);
    return lhs.current_ - rhs.current_;
  }

  constexpr reference operator*() const {
    CHECK_NE(current_, end_);
    return *current_;
  }

  constexpr pointer operator->() const {
    CHECK_NE(current_, end_);
    return current_;
  }

  constexpr reference operator[](difference_type rhs) const {
    CHECK_GE(rhs, 0);
    CHECK_LT(rhs, end_ - current_);
    return current_[rhs];
  }

  static bool IsRangeMoveSafe(const CheckedContiguousIterator& from_begin,
                              const CheckedContiguousIterator& from_end,
                              const CheckedContiguousIterator& to)
      WARN_UNUSED_RESULT {
    if (from_end < from_begin)
      return false;
    const auto from_begin_uintptr = get_uintptr(from_begin.current_);
    const auto from_end_uintptr = get_uintptr(from_end.current_);
    const auto to_begin_uintptr = get_uintptr(to.current_);
    const auto to_end_uintptr =
        get_uintptr((to + std::distance(from_begin, from_end)).current_);

    return to_begin_uintptr >= from_end_uintptr ||
           to_end_uintptr <= from_begin_uintptr;
  }

 private:
  constexpr void CheckComparable(const CheckedContiguousIterator& other) const {
    CHECK_EQ(start_, other.start_);
    CHECK_EQ(end_, other.end_);
  }

  const T* start_ = nullptr;
  T* current_ = nullptr;
  const T* end_ = nullptr;
};

template <typename T>
using CheckedContiguousConstIterator = CheckedContiguousIterator<const T>;

}  // namespace base

#endif  // BASE_CONTAINERS_CHECKED_ITERATORS_H_
