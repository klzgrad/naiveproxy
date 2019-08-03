// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CONTAINERS_CHECKED_ITERATORS_H_
#define BASE_CONTAINERS_CHECKED_ITERATORS_H_

#include <iterator>
#include <memory>

#include "base/containers/util.h"
#include "base/logging.h"

namespace base {

template <typename T>
class CheckedRandomAccessConstIterator;

template <typename T>
class CheckedRandomAccessIterator {
 public:
  using difference_type = std::ptrdiff_t;
  using value_type = typename std::iterator_traits<T*>::value_type;
  using pointer = T*;
  using reference = T&;
  using iterator_category = std::random_access_iterator_tag;

  friend class CheckedRandomAccessConstIterator<T>;

  CheckedRandomAccessIterator() = default;
  CheckedRandomAccessIterator(T* start, const T* end)
      : CheckedRandomAccessIterator(start, start, end) {}
  CheckedRandomAccessIterator(T* start, T* current, const T* end)
      : start_(start), current_(current), end_(end) {
    CHECK(start <= current);
    CHECK(current <= end);
  }
  CheckedRandomAccessIterator(const CheckedRandomAccessIterator& other) =
      default;
  ~CheckedRandomAccessIterator() = default;

  CheckedRandomAccessIterator& operator=(
      const CheckedRandomAccessIterator& other) = default;

  bool operator==(const CheckedRandomAccessIterator& other) const {
    CheckComparable(other);
    return current_ == other.current_;
  }

  bool operator!=(const CheckedRandomAccessIterator& other) const {
    CheckComparable(other);
    return current_ != other.current_;
  }

  bool operator<(const CheckedRandomAccessIterator& other) const {
    CheckComparable(other);
    return current_ < other.current_;
  }

  bool operator<=(const CheckedRandomAccessIterator& other) const {
    CheckComparable(other);
    return current_ <= other.current_;
  }

  CheckedRandomAccessIterator& operator++() {
    CHECK(current_ != end_);
    ++current_;
    return *this;
  }

  CheckedRandomAccessIterator operator++(int) {
    CheckedRandomAccessIterator old = *this;
    ++*this;
    return old;
  }

  CheckedRandomAccessIterator& operator--() {
    CHECK(current_ != start_);
    --current_;
    return *this;
  }

  CheckedRandomAccessIterator& operator--(int) {
    CheckedRandomAccessIterator old = *this;
    --*this;
    return old;
  }

  CheckedRandomAccessIterator& operator+=(difference_type rhs) {
    if (rhs > 0) {
      CHECK_LE(rhs, end_ - current_);
    } else {
      CHECK_LE(-rhs, current_ - start_);
    }
    current_ += rhs;
    return *this;
  }

  CheckedRandomAccessIterator operator+(difference_type rhs) const {
    CheckedRandomAccessIterator it = *this;
    it += rhs;
    return it;
  }

  CheckedRandomAccessIterator& operator-=(difference_type rhs) {
    if (rhs < 0) {
      CHECK_LE(rhs, end_ - current_);
    } else {
      CHECK_LE(-rhs, current_ - start_);
    }
    current_ -= rhs;
    return *this;
  }

  CheckedRandomAccessIterator operator-(difference_type rhs) const {
    CheckedRandomAccessIterator it = *this;
    it -= rhs;
    return it;
  }

  friend difference_type operator-(const CheckedRandomAccessIterator& lhs,
                                   const CheckedRandomAccessIterator& rhs) {
    CHECK(lhs.start_ == rhs.start_);
    CHECK(lhs.end_ == rhs.end_);
    return lhs.current_ - rhs.current_;
  }

  reference operator*() const {
    CHECK(current_ != end_);
    return *current_;
  }

  pointer operator->() const {
    CHECK(current_ != end_);
    return current_;
  }

  static bool IsRangeMoveSafe(const CheckedRandomAccessIterator& from_begin,
                              const CheckedRandomAccessIterator& from_end,
                              const CheckedRandomAccessIterator& to)
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
  void CheckComparable(const CheckedRandomAccessIterator& other) const {
    CHECK_EQ(start_, other.start_);
    CHECK_EQ(end_, other.end_);
  }

  const T* start_ = nullptr;
  T* current_ = nullptr;
  const T* end_ = nullptr;
};

template <typename T>
class CheckedRandomAccessConstIterator {
 public:
  using difference_type = std::ptrdiff_t;
  using value_type = typename std::iterator_traits<T*>::value_type;
  using pointer = const T*;
  using reference = const T&;
  using iterator_category = std::random_access_iterator_tag;

  CheckedRandomAccessConstIterator() = default;
  CheckedRandomAccessConstIterator(T* start, const T* end)
      : CheckedRandomAccessConstIterator(start, start, end) {}
  CheckedRandomAccessConstIterator(T* start, T* current, const T* end)
      : start_(start), current_(current), end_(end) {
    CHECK(start <= current);
    CHECK(current <= end);
  }
  CheckedRandomAccessConstIterator(
      const CheckedRandomAccessConstIterator& other) = default;
  CheckedRandomAccessConstIterator(const CheckedRandomAccessIterator<T>& other)
      : start_(other.start_), current_(other.current_), end_(other.end_) {
    // We explicitly don't delegate to the 3-argument constructor here. Its
    // CHECKs would be redundant, since we expect |other| to maintain its own
    // invariant. However, DCHECKs never hurt anybody. Presumably.
    DCHECK(other.start_ <= other.current_);
    DCHECK(other.current_ <= other.end_);
  }
  ~CheckedRandomAccessConstIterator() = default;

  CheckedRandomAccessConstIterator& operator=(
      const CheckedRandomAccessConstIterator& other) = default;

  CheckedRandomAccessConstIterator& operator=(
      CheckedRandomAccessConstIterator& other) = default;

  bool operator==(const CheckedRandomAccessConstIterator& other) const {
    CheckComparable(other);
    return current_ == other.current_;
  }

  bool operator!=(const CheckedRandomAccessConstIterator& other) const {
    CheckComparable(other);
    return current_ != other.current_;
  }

  bool operator<(const CheckedRandomAccessConstIterator& other) const {
    CheckComparable(other);
    return current_ < other.current_;
  }

  bool operator<=(const CheckedRandomAccessConstIterator& other) const {
    CheckComparable(other);
    return current_ <= other.current_;
  }

  CheckedRandomAccessConstIterator& operator++() {
    CHECK(current_ != end_);
    ++current_;
    return *this;
  }

  CheckedRandomAccessConstIterator operator++(int) {
    CheckedRandomAccessConstIterator old = *this;
    ++*this;
    return old;
  }

  CheckedRandomAccessConstIterator& operator--() {
    CHECK(current_ != start_);
    --current_;
    return *this;
  }

  CheckedRandomAccessConstIterator& operator--(int) {
    CheckedRandomAccessConstIterator old = *this;
    --*this;
    return old;
  }

  CheckedRandomAccessConstIterator& operator+=(difference_type rhs) {
    if (rhs > 0) {
      CHECK_LE(rhs, end_ - current_);
    } else {
      CHECK_LE(-rhs, current_ - start_);
    }
    current_ += rhs;
    return *this;
  }

  CheckedRandomAccessConstIterator operator+(difference_type rhs) const {
    CheckedRandomAccessConstIterator it = *this;
    it += rhs;
    return it;
  }

  CheckedRandomAccessConstIterator& operator-=(difference_type rhs) {
    if (rhs < 0) {
      CHECK_LE(rhs, end_ - current_);
    } else {
      CHECK_LE(-rhs, current_ - start_);
    }
    current_ -= rhs;
    return *this;
  }

  CheckedRandomAccessConstIterator operator-(difference_type rhs) const {
    CheckedRandomAccessConstIterator it = *this;
    it -= rhs;
    return it;
  }

  friend difference_type operator-(
      const CheckedRandomAccessConstIterator& lhs,
      const CheckedRandomAccessConstIterator& rhs) {
    CHECK(lhs.start_ == rhs.start_);
    CHECK(lhs.end_ == rhs.end_);
    return lhs.current_ - rhs.current_;
  }

  reference operator*() const {
    CHECK(current_ != end_);
    return *current_;
  }

  pointer operator->() const {
    CHECK(current_ != end_);
    return current_;
  }

  static bool IsRangeMoveSafe(
      const CheckedRandomAccessConstIterator& from_begin,
      const CheckedRandomAccessConstIterator& from_end,
      const CheckedRandomAccessConstIterator& to) WARN_UNUSED_RESULT {
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
  void CheckComparable(const CheckedRandomAccessConstIterator& other) const {
    CHECK_EQ(start_, other.start_);
    CHECK_EQ(end_, other.end_);
  }

  const T* start_ = nullptr;
  const T* current_ = nullptr;
  const T* end_ = nullptr;
};

}  // namespace base

#endif  // BASE_CONTAINERS_CHECKED_ITERATORS_H_
