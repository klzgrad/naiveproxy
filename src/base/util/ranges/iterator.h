// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_UTIL_RANGES_ITERATOR_H_
#define BASE_UTIL_RANGES_ITERATOR_H_

#include <iterator>

namespace util {
namespace ranges {

// Simplified implementation of C++20's std::ranges::begin.
// As opposed to std::ranges::begin, this implementation does not prefer a
// member begin() over a free standing begin(), does not check whether begin()
// returns an iterator, does not inhibit ADL and is not constexpr.
//
// Reference: https://wg21.link/range.access.begin
template <typename Range>
decltype(auto) begin(Range&& range) {
  using std::begin;
  return begin(std::forward<Range>(range));
}

// Simplified implementation of C++20's std::ranges::end.
// As opposed to std::ranges::end, this implementation does not prefer a
// member end() over a free standing end(), does not check whether end()
// returns an iterator, does not inhibit ADL and is not constexpr.
//
// Reference: - https://wg21.link/range.access.end
template <typename Range>
decltype(auto) end(Range&& range) {
  using std::end;
  return end(std::forward<Range>(range));
}

}  // namespace ranges
}  // namespace util

#endif  // BASE_UTIL_RANGES_ITERATOR_H_
