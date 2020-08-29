// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_UTIL_RANGES_ALGORITHM_H_
#define BASE_UTIL_RANGES_ALGORITHM_H_

#include <algorithm>
#include <iterator>
#include <utility>

#include "base/util/ranges/functional.h"
#include "base/util/ranges/iterator.h"

namespace util {
namespace ranges {

namespace internal {

// Returns a transformed version of the unary predicate `pred` applying `proj`
// to its argument before invoking `pred` on it.
// Ensures that the return type of `invoke(pred, ...)` is convertible to bool.
template <typename Pred, typename Proj>
constexpr auto ProjectedUnaryPredicate(Pred& pred, Proj& proj) noexcept {
  return [&pred, &proj](auto&& arg) -> bool {
    return invoke(pred, invoke(proj, std::forward<decltype(arg)>(arg)));
  };
}

// Returns a transformed version of the binary predicate `pred` applying `proj1`
// and `proj2` to its arguments before invoking `pred` on them.
// Ensures that the return type of `invoke(pred, ...)` is convertible to bool.
template <typename Pred, typename Proj1, typename Proj2>
constexpr auto ProjectedBinaryPredicate(Pred& pred,
                                        Proj1& proj1,
                                        Proj2& proj2) noexcept {
  return [&pred, &proj1, &proj2](auto&& lhs, auto&& rhs) -> bool {
    return invoke(pred, invoke(proj1, std::forward<decltype(lhs)>(lhs)),
                  invoke(proj2, std::forward<decltype(rhs)>(rhs)));
  };
}

// This alias is used below to restrict iterator based APIs to types for which
// `iterator_category` is defined. This is required in situations where
// otherwise an undesired overload would be chosen, e.g. copy_if. In spirit this
// is similar to C++20's std::input_or_output_iterator, a concept that each
// iterator should satisfy.
template <typename Iter>
using iterator_category_t =
    typename std::iterator_traits<Iter>::iterator_category;

}  // namespace internal

// [alg.nonmodifying] Non-modifying sequence operations
// Reference: https://wg21.link/alg.nonmodifying

// [alg.all.of] All of
// Reference: https://wg21.link/alg.all.of

// Let `E(i)` be `invoke(pred, invoke(proj, *i))`.
//
// Returns: `false` if `E(i)` is `false` for some iterator `i` in the range
// `[first, last)`, and `true` otherwise.
//
// Complexity: At most `last - first` applications of the predicate and any
// projection.
//
// Reference: https://wg21.link/alg.all.of#:~:text=ranges::all_of(I
template <typename InputIterator,
          typename Pred,
          typename Proj = identity,
          typename = internal::iterator_category_t<InputIterator>>
constexpr bool all_of(InputIterator first,
                      InputIterator last,
                      Pred pred,
                      Proj proj = {}) {
  return std::all_of(first, last,
                     internal::ProjectedUnaryPredicate(pred, proj));
}

// Let `E(i)` be `invoke(pred, invoke(proj, *i))`.
//
// Returns: `false` if `E(i)` is `false` for some iterator `i` in `range`, and
// `true` otherwise.
//
// Complexity: At most `size(range)` applications of the predicate and any
// projection.
//
// Reference: https://wg21.link/alg.all.of#:~:text=ranges::all_of(R
template <typename Range, typename Pred, typename Proj = identity>
constexpr bool all_of(Range&& range, Pred pred, Proj proj = {}) {
  return ranges::all_of(ranges::begin(range), ranges::end(range),
                        std::move(pred), std::move(proj));
}

// [alg.any.of] Any of
// Reference: https://wg21.link/alg.any.of

// Let `E(i)` be `invoke(pred, invoke(proj, *i))`.
//
// Returns: `true` if `E(i)` is `true` for some iterator `i` in the range
// `[first, last)`, and `false` otherwise.
//
// Complexity: At most `last - first` applications of the predicate and any
// projection.
//
// Reference: https://wg21.link/alg.any.of#:~:text=ranges::any_of(I
template <typename InputIterator,
          typename Pred,
          typename Proj = identity,
          typename = internal::iterator_category_t<InputIterator>>
constexpr bool any_of(InputIterator first,
                      InputIterator last,
                      Pred pred,
                      Proj proj = {}) {
  return std::any_of(first, last,
                     internal::ProjectedUnaryPredicate(pred, proj));
}

// Let `E(i)` be `invoke(pred, invoke(proj, *i))`.
//
// Returns: `true` if `E(i)` is `true` for some iterator `i` in `range`, and
// `false` otherwise.
//
// Complexity: At most `size(range)` applications of the predicate and any
// projection.
//
// Reference: https://wg21.link/alg.any.of#:~:text=ranges::any_of(R
template <typename Range, typename Pred, typename Proj = identity>
constexpr bool any_of(Range&& range, Pred pred, Proj proj = {}) {
  return ranges::any_of(ranges::begin(range), ranges::end(range),
                        std::move(pred), std::move(proj));
}

// [alg.none.of] None of
// Reference: https://wg21.link/alg.none.of

// Let `E(i)` be `invoke(pred, invoke(proj, *i))`.
//
// Returns: `false` if `E(i)` is `true` for some iterator `i` in the range
// `[first, last)`, and `true` otherwise.
//
// Complexity: At most `last - first` applications of the predicate and any
// projection.
//
// Reference: https://wg21.link/alg.none.of#:~:text=ranges::none_of(I
template <typename InputIterator,
          typename Pred,
          typename Proj = identity,
          typename = internal::iterator_category_t<InputIterator>>
constexpr bool none_of(InputIterator first,
                       InputIterator last,
                       Pred pred,
                       Proj proj = {}) {
  return std::none_of(first, last,
                      internal::ProjectedUnaryPredicate(pred, proj));
}

// Let `E(i)` be `invoke(pred, invoke(proj, *i))`.
//
// Returns: `false` if `E(i)` is `true` for some iterator `i` in `range`, and
// `true` otherwise.
//
// Complexity: At most `size(range)` applications of the predicate and any
// projection.
//
// Reference: https://wg21.link/alg.none.of#:~:text=ranges::none_of(R
template <typename Range, typename Pred, typename Proj = identity>
constexpr bool none_of(Range&& range, Pred pred, Proj proj = {}) {
  return ranges::none_of(ranges::begin(range), ranges::end(range),
                         std::move(pred), std::move(proj));
}

// [alg.foreach] For each
// Reference: https://wg21.link/alg.foreach

// Effects: Calls `invoke(f, invoke(proj, *i))` for every iterator `i` in the
// range `[first, last)`, starting from `first` and proceeding to `last - 1`.
//
// Returns: `f`
// Note: std::ranges::for_each(I first,...) returns a for_each_result, rather
// than an invocable. For simplicitly we match std::for_each's return type
// instead.
//
// Complexity: Applies `f` and `proj` exactly `last - first` times.
//
// Remarks: If `f` returns a result, the result is ignored.
//
// Reference: https://wg21.link/alg.foreach#:~:text=ranges::for_each(I
template <typename InputIterator,
          typename Fun,
          typename Proj = identity,
          typename = internal::iterator_category_t<InputIterator>>
constexpr auto for_each(InputIterator first,
                        InputIterator last,
                        Fun f,
                        Proj proj = {}) {
  std::for_each(first, last, [&f, &proj](auto&& arg) {
    return invoke(f, invoke(proj, std::forward<decltype(arg)>(arg)));
  });

  return f;
}

// Effects: Calls `invoke(f, invoke(proj, *i))` for every iterator `i` in the
// range `range`, starting from `begin(range)` and proceeding to `end(range) -
// 1`.
//
// Returns: `f`
// Note: std::ranges::for_each(R&& r,...) returns a for_each_result, rather
// than an invocable. For simplicitly we match std::for_each's return type
// instead.
//
// Complexity: Applies `f` and `proj` exactly `size(range)` times.
//
// Remarks: If `f` returns a result, the result is ignored.
//
// Reference: https://wg21.link/alg.foreach#:~:text=ranges::for_each(R
template <typename Range, typename Fun, typename Proj = identity>
constexpr auto for_each(Range&& range, Fun f, Proj proj = {}) {
  return ranges::for_each(ranges::begin(range), ranges::end(range),
                          std::move(f), std::move(proj));
}

// [alg.find] Find
// Reference: https://wg21.link/alg.find

// Let `E(i)` be `bool(invoke(proj, *i) == value)`.
//
// Returns: The first iterator `i` in the range `[first, last)` for which `E(i)`
// is `true`. Returns `last` if no such iterator is found.
//
// Complexity: At most `last - first` applications of the corresponding
// predicate and any projection.
//
// Reference: https://wg21.link/alg.find#:~:text=ranges::find(I
template <typename InputIterator,
          typename T,
          typename Proj = identity,
          typename = internal::iterator_category_t<InputIterator>>
constexpr auto find(InputIterator first,
                    InputIterator last,
                    const T& value,
                    Proj proj = {}) {
  // Note: In order to be able to apply `proj` to each element in [first, last)
  // we are dispatching to std::find_if instead of std::find.
  return std::find_if(first, last, [&proj, &value](auto&& lhs) {
    return invoke(proj, std::forward<decltype(lhs)>(lhs)) == value;
  });
}

// Let `E(i)` be `bool(invoke(proj, *i) == value)`.
//
// Returns: The first iterator `i` in `range` for which `E(i)` is `true`.
// Returns `end(range)` if no such iterator is found.
//
// Complexity: At most `size(range)` applications of the corresponding predicate
// and any projection.
//
// Reference: https://wg21.link/alg.find#:~:text=ranges::find(R
template <typename Range, typename T, typename Proj = identity>
constexpr auto find(Range&& range, const T& value, Proj proj = {}) {
  return ranges::find(ranges::begin(range), ranges::end(range), value,
                      std::move(proj));
}

// Let `E(i)` be `bool(invoke(pred, invoke(proj, *i)))`.
//
// Returns: The first iterator `i` in the range `[first, last)` for which `E(i)`
// is `true`. Returns `last` if no such iterator is found.
//
// Complexity: At most `last - first` applications of the corresponding
// predicate and any projection.
//
// Reference: https://wg21.link/alg.find#:~:text=ranges::find_if(I
template <typename InputIterator,
          typename Pred,
          typename Proj = identity,
          typename = internal::iterator_category_t<InputIterator>>
constexpr auto find_if(InputIterator first,
                       InputIterator last,
                       Pred pred,
                       Proj proj = {}) {
  return std::find_if(first, last,
                      internal::ProjectedUnaryPredicate(pred, proj));
}

// Let `E(i)` be `bool(invoke(pred, invoke(proj, *i)))`.
//
// Returns: The first iterator `i` in `range` for which `E(i)` is `true`.
// Returns `end(range)` if no such iterator is found.
//
// Complexity: At most `size(range)` applications of the corresponding predicate
// and any projection.
//
// Reference: https://wg21.link/alg.find#:~:text=ranges::find_if(R
template <typename Range, typename Pred, typename Proj = identity>
constexpr auto find_if(Range&& range, Pred pred, Proj proj = {}) {
  return ranges::find_if(ranges::begin(range), ranges::end(range),
                         std::move(pred), std::move(proj));
}

// Let `E(i)` be `bool(!invoke(pred, invoke(proj, *i)))`.
//
// Returns: The first iterator `i` in the range `[first, last)` for which `E(i)`
// is `true`. Returns `last` if no such iterator is found.
//
// Complexity: At most `last - first` applications of the corresponding
// predicate and any projection.
//
// Reference: https://wg21.link/alg.find#:~:text=ranges::find_if_not(I
template <typename InputIterator,
          typename Pred,
          typename Proj = identity,
          typename = internal::iterator_category_t<InputIterator>>
constexpr auto find_if_not(InputIterator first,
                           InputIterator last,
                           Pred pred,
                           Proj proj = {}) {
  return std::find_if_not(first, last,
                          internal::ProjectedUnaryPredicate(pred, proj));
}

// Let `E(i)` be `bool(!invoke(pred, invoke(proj, *i)))`.
//
// Returns: The first iterator `i` in `range` for which `E(i)` is `true`.
// Returns `end(range)` if no such iterator is found.
//
// Complexity: At most `size(range)` applications of the corresponding predicate
// and any projection.
//
// Reference: https://wg21.link/alg.find#:~:text=ranges::find_if_not(R
template <typename Range, typename Pred, typename Proj = identity>
constexpr auto find_if_not(Range&& range, Pred pred, Proj proj = {}) {
  return ranges::find_if_not(ranges::begin(range), ranges::end(range),
                             std::move(pred), std::move(proj));
}

// [alg.find.end] Find end
// Reference: https://wg21.link/alg.find.end

// Let:
// - `E(i,n)` be `invoke(pred, invoke(proj1, *(i + n)),
//                             invoke(proj2, *(first2 + n)))`
//
// - `i` be `last1` if `[first2, last2)` is empty, or if
//   `(last2 - first2) > (last1 - first1)` is `true`, or if there is no iterator
//   in the range `[first1, last1 - (last2 - first2))` such that for every
//   non-negative integer `n < (last2 - first2)`, `E(i,n)` is `true`. Otherwise
//   `i` is the last such iterator in `[first1, last1 - (last2 - first2))`.
//
// Returns: `i`
// Note: std::ranges::find_end(I1 first1,...) returns a range, rather than an
// iterator. For simplicitly we match std::find_end's return type instead.
//
// Complexity:
// At most `(last2 - first2) * (last1 - first1 - (last2 - first2) + 1)`
// applications of the corresponding predicate and any projections.
//
// Reference: https://wg21.link/alg.find.end#:~:text=ranges::find_end(I1
template <typename ForwardIterator1,
          typename ForwardIterator2,
          typename Pred = ranges::equal_to,
          typename Proj1 = identity,
          typename Proj2 = identity,
          typename = internal::iterator_category_t<ForwardIterator1>,
          typename = internal::iterator_category_t<ForwardIterator2>>
constexpr auto find_end(ForwardIterator1 first1,
                        ForwardIterator1 last1,
                        ForwardIterator2 first2,
                        ForwardIterator2 last2,
                        Pred pred = {},
                        Proj1 proj1 = {},
                        Proj2 proj2 = {}) {
  return std::find_end(first1, last1, first2, last2,
                       internal::ProjectedBinaryPredicate(pred, proj1, proj2));
}

// Let:
// - `E(i,n)` be `invoke(pred, invoke(proj1, *(i + n)),
//                             invoke(proj2, *(first2 + n)))`
//
// - `i` be `end(range1)` if `range2` is empty, or if
//   `size(range2) > size(range1)` is `true`, or if there is no iterator in the
//   range `[begin(range1), end(range1) - size(range2))` such that for every
//   non-negative integer `n < size(range2)`, `E(i,n)` is `true`. Otherwise `i`
//   is the last such iterator in `[begin(range1), end(range1) - size(range2))`.
//
// Returns: `i`
// Note: std::ranges::find_end(R1&& r1,...) returns a range, rather than an
// iterator. For simplicitly we match std::find_end's return type instead.
//
// Complexity: At most `size(range2) * (size(range1) - size(range2) + 1)`
// applications of the corresponding predicate and any projections.
//
// Reference: https://wg21.link/alg.find.end#:~:text=ranges::find_end(R1
template <typename Range1,
          typename Range2,
          typename Pred = ranges::equal_to,
          typename Proj1 = identity,
          typename Proj2 = identity>
constexpr auto find_end(Range1&& range1,
                        Range2&& range2,
                        Pred pred = {},
                        Proj1 proj1 = {},
                        Proj2 proj2 = {}) {
  return ranges::find_end(ranges::begin(range1), ranges::end(range1),
                          ranges::begin(range2), ranges::end(range2),
                          std::move(pred), std::move(proj1), std::move(proj2));
}

// [alg.find.first.of] Find first
// Reference: https://wg21.link/alg.find.first.of

// Let `E(i,j)` be `bool(invoke(pred, invoke(proj1, *i), invoke(proj2, *j)))`.
//
// Effects: Finds an element that matches one of a set of values.
//
// Returns: The first iterator `i` in the range `[first1, last1)` such that for
// some iterator `j` in the range `[first2, last2)` `E(i,j)` holds. Returns
// `last1` if `[first2, last2)` is empty or if no such iterator is found.
//
// Complexity: At most `(last1 - first1) * (last2 - first2)` applications of the
// corresponding predicate and any projections.
//
// Reference:
// https://wg21.link/alg.find.first.of#:~:text=ranges::find_first_of(I1
template <typename ForwardIterator1,
          typename ForwardIterator2,
          typename Pred = ranges::equal_to,
          typename Proj1 = identity,
          typename Proj2 = identity,
          typename = internal::iterator_category_t<ForwardIterator1>,
          typename = internal::iterator_category_t<ForwardIterator2>>
constexpr auto find_first_of(ForwardIterator1 first1,
                             ForwardIterator1 last1,
                             ForwardIterator2 first2,
                             ForwardIterator2 last2,
                             Pred pred = {},
                             Proj1 proj1 = {},
                             Proj2 proj2 = {}) {
  return std::find_first_of(
      first1, last1, first2, last2,
      internal::ProjectedBinaryPredicate(pred, proj1, proj2));
}

// Let `E(i,j)` be `bool(invoke(pred, invoke(proj1, *i), invoke(proj2, *j)))`.
//
// Effects: Finds an element that matches one of a set of values.
//
// Returns: The first iterator `i` in `range1` such that for some iterator `j`
// in `range2` `E(i,j)` holds. Returns `end(range1)` if `range2` is empty or if
// no such iterator is found.
//
// Complexity: At most `size(range1) * size(range2)` applications of the
// corresponding predicate and any projections.
//
// Reference:
// https://wg21.link/alg.find.first.of#:~:text=ranges::find_first_of(R1
template <typename Range1,
          typename Range2,
          typename Pred = ranges::equal_to,
          typename Proj1 = identity,
          typename Proj2 = identity>
constexpr auto find_first_of(Range1&& range1,
                             Range2&& range2,
                             Pred pred = {},
                             Proj1 proj1 = {},
                             Proj2 proj2 = {}) {
  return ranges::find_first_of(
      ranges::begin(range1), ranges::end(range1), ranges::begin(range2),
      ranges::end(range2), std::move(pred), std::move(proj1), std::move(proj2));
}

// [alg.adjacent.find] Adjacent find
// Reference: https://wg21.link/alg.adjacent.find

// Let `E(i)` be `bool(invoke(pred, invoke(proj, *i), invoke(proj, *(i + 1))))`.
//
// Returns: The first iterator `i` such that both `i` and `i + 1` are in the
// range `[first, last)` for which `E(i)` holds. Returns `last` if no such
// iterator is found.
//
// Complexity: Exactly `min((i - first) + 1, (last - first) - 1)` applications
// of the corresponding predicate, where `i` is `adjacent_find`'s return value.
//
// Reference:
// https://wg21.link/alg.adjacent.find#:~:text=ranges::adjacent_find(I
template <typename ForwardIterator,
          typename Pred = ranges::equal_to,
          typename Proj = identity,
          typename = internal::iterator_category_t<ForwardIterator>>
constexpr auto adjacent_find(ForwardIterator first,
                             ForwardIterator last,
                             Pred pred = {},
                             Proj proj = {}) {
  return std::adjacent_find(
      first, last, internal::ProjectedBinaryPredicate(pred, proj, proj));
}

// Let `E(i)` be `bool(invoke(pred, invoke(proj, *i), invoke(proj, *(i + 1))))`.
//
// Returns: The first iterator `i` such that both `i` and `i + 1` are in the
// range `range` for which `E(i)` holds. Returns `end(range)` if no such
// iterator is found.
//
// Complexity: Exactly `min((i - begin(range)) + 1, size(range) - 1)`
// applications of the corresponding predicate, where `i` is `adjacent_find`'s
// return value.
//
// Reference:
// https://wg21.link/alg.adjacent.find#:~:text=ranges::adjacent_find(R
template <typename Range,
          typename Pred = ranges::equal_to,
          typename Proj = identity>
constexpr auto adjacent_find(Range&& range, Pred pred = {}, Proj proj = {}) {
  return ranges::adjacent_find(ranges::begin(range), ranges::end(range),
                               std::move(pred), std::move(proj));
}

// [alg.count] Count
// Reference: https://wg21.link/alg.count

// Let `E(i)` be `invoke(proj, *i) == value`.
//
// Effects: Returns the number of iterators `i` in the range `[first, last)` for
// which `E(i)` holds.
//
// Complexity: Exactly `last - first` applications of the corresponding
// predicate and any projection.
//
// Reference: https://wg21.link/alg.count#:~:text=ranges::count(I
template <typename InputIterator,
          typename T,
          typename Proj = identity,
          typename = internal::iterator_category_t<InputIterator>>
constexpr auto count(InputIterator first,
                     InputIterator last,
                     const T& value,
                     Proj proj = {}) {
  // Note: In order to be able to apply `proj` to each element in [first, last)
  // we are dispatching to std::count_if instead of std::count.
  return std::count_if(first, last, [&proj, &value](auto&& lhs) {
    return invoke(proj, std::forward<decltype(lhs)>(lhs)) == value;
  });
}

// Let `E(i)` be `invoke(proj, *i) == value`.
//
// Effects: Returns the number of iterators `i` in `range` for which `E(i)`
// holds.
//
// Complexity: Exactly `size(range)` applications of the corresponding predicate
// and any projection.
//
// Reference: https://wg21.link/alg.count#:~:text=ranges::count(R
template <typename Range, typename T, typename Proj = identity>
constexpr auto count(Range&& range, const T& value, Proj proj = {}) {
  return ranges::count(ranges::begin(range), ranges::end(range), value,
                       std::move(proj));
}

// Let `E(i)` be `bool(invoke(pred, invoke(proj, *i)))`.
//
// Effects: Returns the number of iterators `i` in the range `[first, last)` for
// which `E(i)` holds.
//
// Complexity: Exactly `last - first` applications of the corresponding
// predicate and any projection.
//
// Reference: https://wg21.link/alg.count#:~:text=ranges::count_if(I
template <typename InputIterator,
          typename Pred,
          typename Proj = identity,
          typename = internal::iterator_category_t<InputIterator>>
constexpr auto count_if(InputIterator first,
                        InputIterator last,
                        Pred pred,
                        Proj proj = {}) {
  return std::count_if(first, last,
                       internal::ProjectedUnaryPredicate(pred, proj));
}

// Let `E(i)` be `bool(invoke(pred, invoke(proj, *i)))`.
//
// Effects: Returns the number of iterators `i` in `range` for which `E(i)`
// holds.
//
// Complexity: Exactly `size(range)` applications of the corresponding predicate
// and any projection.
//
// Reference: https://wg21.link/alg.count#:~:text=ranges::count_if(R
template <typename Range, typename Pred, typename Proj = identity>
constexpr auto count_if(Range&& range, Pred pred, Proj proj = {}) {
  return ranges::count_if(ranges::begin(range), ranges::end(range),
                          std::move(pred), std::move(proj));
}

// [mismatch] Mismatch
// Reference: https://wg21.link/mismatch

// Let `E(n)` be `!invoke(pred, invoke(proj1, *(first1 + n)),
//                              invoke(proj2, *(first2 + n)))`.
//
// Let `N` be `min(last1 - first1, last2 - first2)`.
//
// Returns: `{ first1 + n, first2 + n }`, where `n` is the smallest integer in
// `[0, N)` such that `E(n)` holds, or `N` if no such integer exists.
//
// Complexity: At most `N` applications of the corresponding predicate and any
// projections.
//
// Reference: https://wg21.link/mismatch#:~:text=ranges::mismatch(I1
template <typename ForwardIterator1,
          typename ForwardIterator2,
          typename Pred = ranges::equal_to,
          typename Proj1 = identity,
          typename Proj2 = identity,
          typename = internal::iterator_category_t<ForwardIterator1>,
          typename = internal::iterator_category_t<ForwardIterator2>>
constexpr auto mismatch(ForwardIterator1 first1,
                        ForwardIterator1 last1,
                        ForwardIterator2 first2,
                        ForwardIterator2 last2,
                        Pred pred = {},
                        Proj1 proj1 = {},
                        Proj2 proj2 = {}) {
  return std::mismatch(first1, last1, first2, last2,
                       internal::ProjectedBinaryPredicate(pred, proj1, proj2));
}

// Let `E(n)` be `!invoke(pred, invoke(proj1, *(begin(range1) + n)),
//                              invoke(proj2, *(begin(range2) + n)))`.
//
// Let `N` be `min(size(range1), size(range2))`.
//
// Returns: `{ begin(range1) + n, begin(range2) + n }`, where `n` is the
// smallest integer in `[0, N)` such that `E(n)` holds, or `N` if no such
// integer exists.
//
// Complexity: At most `N` applications of the corresponding predicate and any
// projections.
//
// Reference: https://wg21.link/mismatch#:~:text=ranges::mismatch(R1
template <typename Range1,
          typename Range2,
          typename Pred = ranges::equal_to,
          typename Proj1 = identity,
          typename Proj2 = identity>
constexpr auto mismatch(Range1&& range1,
                        Range2&& range2,
                        Pred pred = {},
                        Proj1 proj1 = {},
                        Proj2 proj2 = {}) {
  return ranges::mismatch(ranges::begin(range1), ranges::end(range1),
                          ranges::begin(range2), ranges::end(range2),
                          std::move(pred), std::move(proj1), std::move(proj2));
}

// [alg.equal] Equal
// Reference: https://wg21.link/alg.equal

// Let `E(i)` be
//   `invoke(pred, invoke(proj1, *i), invoke(proj2, *(first2 + (i - first1))))`.
//
// Returns: If `last1 - first1 != last2 - first2`, return `false.` Otherwise
// return `true` if `E(i)` holds for every iterator `i` in the range `[first1,
// last1)`. Otherwise, returns `false`.
//
// Complexity: If the types of `first1`, `last1`, `first2`, and `last2` meet the
// `RandomAccessIterator` requirements and `last1 - first1 != last2 - first2`,
// then no applications of the corresponding predicate and each projection;
// otherwise, at most `min(last1 - first1, last2 - first2)` applications of the
// corresponding predicate and any projections.
//
// Reference: https://wg21.link/alg.equal#:~:text=ranges::equal(I1
template <typename ForwardIterator1,
          typename ForwardIterator2,
          typename Pred = ranges::equal_to,
          typename Proj1 = identity,
          typename Proj2 = identity,
          typename = internal::iterator_category_t<ForwardIterator1>,
          typename = internal::iterator_category_t<ForwardIterator2>>
constexpr bool equal(ForwardIterator1 first1,
                     ForwardIterator1 last1,
                     ForwardIterator2 first2,
                     ForwardIterator2 last2,
                     Pred pred = {},
                     Proj1 proj1 = {},
                     Proj2 proj2 = {}) {
  return std::equal(first1, last1, first2, last2,
                    internal::ProjectedBinaryPredicate(pred, proj1, proj2));
}

// Let `E(i)` be
//   `invoke(pred, invoke(proj1, *i),
//                 invoke(proj2, *(begin(range2) + (i - begin(range1)))))`.
//
// Returns: If `size(range1) != size(range2)`, return `false.` Otherwise return
// `true` if `E(i)` holds for every iterator `i` in `range1`. Otherwise, returns
// `false`.
//
// Complexity: If the types of `begin(range1)`, `end(range1)`, `begin(range2)`,
// and `end(range2)` meet the `RandomAccessIterator` requirements and
// `size(range1) != size(range2)`, then no applications of the corresponding
// predicate and each projection;
// otherwise, at most `min(size(range1), size(range2))` applications of the
// corresponding predicate and any projections.
//
// Reference: https://wg21.link/alg.equal#:~:text=ranges::equal(R1
template <typename Range1,
          typename Range2,
          typename Pred = ranges::equal_to,
          typename Proj1 = identity,
          typename Proj2 = identity>
constexpr bool equal(Range1&& range1,
                     Range2&& range2,
                     Pred pred = {},
                     Proj1 proj1 = {},
                     Proj2 proj2 = {}) {
  return ranges::equal(ranges::begin(range1), ranges::end(range1),
                       ranges::begin(range2), ranges::end(range2),
                       std::move(pred), std::move(proj1), std::move(proj2));
}

// [alg.is.permutation] Is permutation
// Reference: https://wg21.link/alg.is.permutation

// Returns: If `last1 - first1 != last2 - first2`, return `false`. Otherwise
// return `true` if there exists a permutation of the elements in the range
// `[first2, last2)`, bounded by `[pfirst, plast)`, such that
// `ranges::equal(first1, last1, pfirst, plast, pred, proj, proj)` returns
// `true`; otherwise, returns `false`.
//
// Complexity: No applications of the corresponding predicate if
// ForwardIterator1 and ForwardIterator2 meet the requirements of random access
// iterators and `last1 - first1 != last2 - first2`. Otherwise, exactly
// `last1 - first1` applications of the corresponding predicate and projections
// if `ranges::equal(first1, last1, first2, last2, pred, proj, proj)` would
// return true;
// otherwise, at worst `O(N^2)`, where `N` has the value `last1 - first1`.
//
// Note: While std::ranges::is_permutation supports different projections for
// the first and second range, this is currently not supported due to
// dispatching to std::is_permutation, which demands that `pred` is an
// equivalence relation.
// TODO(https://crbug.com/1071094): Consider supporing different projections in
// the future.
//
// Reference:
// https://wg21.link/alg.is.permutation#:~:text=ranges::is_permutation(I1
template <typename ForwardIterator1,
          typename ForwardIterator2,
          typename Pred = ranges::equal_to,
          typename Proj = identity,
          typename = internal::iterator_category_t<ForwardIterator1>,
          typename = internal::iterator_category_t<ForwardIterator2>>
constexpr bool is_permutation(ForwardIterator1 first1,
                              ForwardIterator1 last1,
                              ForwardIterator2 first2,
                              ForwardIterator2 last2,
                              Pred pred = {},
                              Proj proj = {}) {
  return std::is_permutation(
      first1, last1, first2, last2,
      internal::ProjectedBinaryPredicate(pred, proj, proj));
}

// Returns: If `size(range1) != size(range2)`, return `false`. Otherwise return
// `true` if there exists a permutation of the elements in `range2`, bounded by
// `[pbegin, pend)`, such that
// `ranges::equal(range1, [pbegin, pend), pred, proj, proj)` returns `true`;
// otherwise, returns `false`.
//
// Complexity: No applications of the corresponding predicate if Range1 and
// Range2 meet the requirements of random access ranges and
// `size(range1) != size(range2)`. Otherwise, exactly `size(range1)`
// applications of the corresponding predicate and projections if
// `ranges::equal(range1, range2, pred, proj, proj)` would return true;
// otherwise, at worst `O(N^2)`, where `N` has the value `size(range1)`.
//
// Note: While std::ranges::is_permutation supports different projections for
// the first and second range, this is currently not supported due to
// dispatching to std::is_permutation, which demands that `pred` is an
// equivalence relation.
// TODO(https://crbug.com/1071094): Consider supporing different projections in
// the future.
//
// Reference:
// https://wg21.link/alg.is.permutation#:~:text=ranges::is_permutation(R1
template <typename Range1,
          typename Range2,
          typename Pred = ranges::equal_to,
          typename Proj = identity>
constexpr bool is_permutation(Range1&& range1,
                              Range2&& range2,
                              Pred pred = {},
                              Proj proj = {}) {
  return ranges::is_permutation(ranges::begin(range1), ranges::end(range1),
                                ranges::begin(range2), ranges::end(range2),
                                std::move(pred), std::move(proj));
}

// [alg.search] Search
// Reference: https://wg21.link/alg.search

// Returns: `i`, where `i` is the first iterator in the range
// `[first1, last1 - (last2 - first2))` such that for every non-negative integer
// `n` less than `last2 - first2` the condition
// `bool(invoke(pred, invoke(proj1, *(i + n)), invoke(proj2, *(first2 + n))))`
// is `true`.
// Returns `last1` if no such iterator exists.
// Note: std::ranges::search(I1 first1,...) returns a range, rather than an
// iterator. For simplicitly we match std::search's return type instead.
//
// Complexity: At most `(last1 - first1) * (last2 - first2)` applications of the
// corresponding predicate and projections.
//
// Reference: https://wg21.link/alg.search#:~:text=ranges::search(I1
template <typename ForwardIterator1,
          typename ForwardIterator2,
          typename Pred = ranges::equal_to,
          typename Proj1 = identity,
          typename Proj2 = identity,
          typename = internal::iterator_category_t<ForwardIterator1>,
          typename = internal::iterator_category_t<ForwardIterator2>>
constexpr auto search(ForwardIterator1 first1,
                      ForwardIterator1 last1,
                      ForwardIterator2 first2,
                      ForwardIterator2 last2,
                      Pred pred = {},
                      Proj1 proj1 = {},
                      Proj2 proj2 = {}) {
  return std::search(first1, last1, first2, last2,
                     internal::ProjectedBinaryPredicate(pred, proj1, proj2));
}

// Returns: `i`, where `i` is the first iterator in the range
// `[begin(range1), end(range1) - size(range2))` such that for every
// non-negative integer `n` less than `size(range2)` the condition
// `bool(invoke(pred, invoke(proj1, *(i + n)),
//                    invoke(proj2, *(begin(range2) + n))))` is `true`.
// Returns `end(range1)` if no such iterator exists.
// Note: std::ranges::search(R1&& r1,...) returns a range, rather than an
// iterator. For simplicitly we match std::search's return type instead.
//
// Complexity: At most `size(range1) * size(range2)` applications of the
// corresponding predicate and projections.
//
// Reference: https://wg21.link/alg.search#:~:text=ranges::search(R1
template <typename Range1,
          typename Range2,
          typename Pred = ranges::equal_to,
          typename Proj1 = identity,
          typename Proj2 = identity>
constexpr auto search(Range1&& range1,
                      Range2&& range2,
                      Pred pred = {},
                      Proj1 proj1 = {},
                      Proj2 proj2 = {}) {
  return ranges::search(ranges::begin(range1), ranges::end(range1),
                        ranges::begin(range2), ranges::end(range2),
                        std::move(pred), std::move(proj1), std::move(proj2));
}

// Mandates: The type `Size` is convertible to an integral type.
//
// Returns: `i` where `i` is the first iterator in the range
// `[first, last - count)` such that for every non-negative integer `n` less
// than `count`, the following condition holds:
// `invoke(pred, invoke(proj, *(i + n)), value)`.
// Returns `last` if no such iterator is found.
// Note: std::ranges::search_n(I1 first1,...) returns a range, rather than an
// iterator. For simplicitly we match std::search_n's return type instead.
//
// Complexity: At most `last - first` applications of the corresponding
// predicate and projection.
//
// Reference: https://wg21.link/alg.search#:~:text=ranges::search_n(I
template <typename ForwardIterator,
          typename Size,
          typename T,
          typename Pred = ranges::equal_to,
          typename Proj = identity,
          typename = internal::iterator_category_t<ForwardIterator>>
constexpr auto search_n(ForwardIterator first,
                        ForwardIterator last,
                        Size count,
                        const T& value,
                        Pred pred = {},
                        Proj proj = {}) {
  // The second arg is guaranteed to be `value`, so we'll simply apply the
  // identity projection.
  identity value_proj;
  return std::search_n(
      first, last, count, value,
      internal::ProjectedBinaryPredicate(pred, proj, value_proj));
}

// Mandates: The type `Size` is convertible to an integral type.
//
// Returns: `i` where `i` is the first iterator in the range
// `[begin(range), end(range) - count)` such that for every non-negative integer
// `n` less than `count`, the following condition holds:
// `invoke(pred, invoke(proj, *(i + n)), value)`.
// Returns `end(arnge)` if no such iterator is found.
// Note: std::ranges::search_n(R1&& r1,...) returns a range, rather than an
// iterator. For simplicitly we match std::search_n's return type instead.
//
// Complexity: At most `size(range)` applications of the corresponding predicate
// and projection.
//
// Reference: https://wg21.link/alg.search#:~:text=ranges::search_n(R
template <typename Range,
          typename Size,
          typename T,
          typename Pred = ranges::equal_to,
          typename Proj = identity>
constexpr auto search_n(Range&& range,
                        Size count,
                        const T& value,
                        Pred pred = {},
                        Proj proj = {}) {
  return ranges::search_n(ranges::begin(range), ranges::end(range), count,
                          value, std::move(pred), std::move(proj));
}

// [alg.modifying.operations] Mutating sequence operations
// Reference: https://wg21.link/alg.modifying.operations

// [alg.copy] Copy
// Reference: https://wg21.link/alg.copy

// Let N be `last - first`.
//
// Preconditions: `result` is not in the range `[first, last)`.
//
// Effects: Copies elements in the range `[first, last)` into the range
// `[result, result + N)` starting from `first` and proceeding to `last`. For
// each non-negative integer `n < N` , performs `*(result + n) = *(first + n)`.
//
// Returns: `result + N`
//
// Complexity: Exactly `N` assignments.
//
// Reference: https://wg21.link/alg.copy#:~:text=ranges::copy(I
template <typename InputIterator,
          typename OutputIterator,
          typename = internal::iterator_category_t<InputIterator>,
          typename = internal::iterator_category_t<OutputIterator>>
constexpr auto copy(InputIterator first,
                    InputIterator last,
                    OutputIterator result) {
  return std::copy(first, last, result);
}

// Let N be `size(range)`.
//
// Preconditions: `result` is not in `range`.
//
// Effects: Copies elements in `range` into the range `[result, result + N)`
// starting from `begin(range)` and proceeding to `end(range)`. For each
// non-negative integer `n < N` , performs
// *(result + n) = *(begin(range) + n)`.
//
// Returns: `result + N`
//
// Complexity: Exactly `N` assignments.
//
// Reference: https://wg21.link/alg.copy#:~:text=ranges::copy(R
template <typename Range,
          typename OutputIterator,
          typename = internal::iterator_category_t<OutputIterator>>
constexpr auto copy(Range&& range, OutputIterator result) {
  return ranges::copy(ranges::begin(range), ranges::end(range), result);
}

// Let `N` be `max(0, n)`.
//
// Mandates: The type `Size` is convertible to an integral type.
//
// Effects: For each non-negative integer `i < N`, performs
// `*(result + i) = *(first + i)`.
//
// Returns: `result + N`
//
// Complexity: Exactly `N` assignments.
//
// Reference: https://wg21.link/alg.copy#:~:text=ranges::copy_n
template <typename InputIterator,
          typename Size,
          typename OutputIterator,
          typename = internal::iterator_category_t<InputIterator>,
          typename = internal::iterator_category_t<OutputIterator>>
constexpr auto copy_n(InputIterator first, Size n, OutputIterator result) {
  return std::copy_n(first, n, result);
}

// Let `E(i)` be `bool(invoke(pred, invoke(proj, *i)))`, and `N` be the number
// of iterators `i` in the range `[first, last)` for which the condition `E(i)`
// holds.
//
// Preconditions: The ranges `[first, last)` and
// `[result, result + (last - first))` do not overlap.
//
// Effects: Copies all of the elements referred to by the iterator `i` in the
// range `[first, last)` for which `E(i)` is true.
//
// Returns: `result + N`
//
// Complexity: Exactly `last - first` applications of the corresponding
// predicate and any projection.
//
// Remarks: Stable.
//
// Reference: https://wg21.link/alg.copy#:~:text=ranges::copy_if(I
template <typename InputIterator,
          typename OutputIterator,
          typename Pred,
          typename Proj = identity,
          typename = internal::iterator_category_t<InputIterator>,
          typename = internal::iterator_category_t<OutputIterator>>
constexpr auto copy_if(InputIterator first,
                       InputIterator last,
                       OutputIterator result,
                       Pred pred,
                       Proj proj = {}) {
  return std::copy_if(first, last, result,
                      internal::ProjectedUnaryPredicate(pred, proj));
}

// Let `E(i)` be `bool(invoke(pred, invoke(proj, *i)))`, and `N` be the number
// of iterators `i` in `range` for which the condition `E(i)` holds.
//
// Preconditions: `range`  and `[result, result + size(range))` do not overlap.
//
// Effects: Copies all of the elements referred to by the iterator `i` in
// `range` for which `E(i)` is true.
//
// Returns: `result + N`
//
// Complexity: Exactly `size(range)` applications of the corresponding predicate
// and any projection.
//
// Remarks: Stable.
//
// Reference: https://wg21.link/alg.copy#:~:text=ranges::copy_if(R
template <typename Range,
          typename OutputIterator,
          typename Pred,
          typename Proj = identity,
          typename = internal::iterator_category_t<OutputIterator>>
constexpr auto copy_if(Range&& range,
                       OutputIterator result,
                       Pred pred,
                       Proj proj = {}) {
  return ranges::copy_if(ranges::begin(range), ranges::end(range), result,
                         std::move(pred), std::move(proj));
}

// Let `N` be `last - first`.
//
// Preconditions: `result` is not in the range `(first, last]`.
//
// Effects: Copies elements in the range `[first, last)` into the range
// `[result - N, result)` starting from `last - 1` and proceeding to `first`.
// For each positive integer `n ≤ N`, performs `*(result - n) = *(last - n)`.
//
// Returns: `result - N`
//
// Complexity: Exactly `N` assignments.
//
// Reference: https://wg21.link/alg.copy#:~:text=ranges::copy_backward(I
template <typename BidirectionalIterator1,
          typename BidirectionalIterator2,
          typename = internal::iterator_category_t<BidirectionalIterator1>,
          typename = internal::iterator_category_t<BidirectionalIterator2>>
constexpr auto copy_backward(BidirectionalIterator1 first,
                             BidirectionalIterator1 last,
                             BidirectionalIterator2 result) {
  return std::copy_backward(first, last, result);
}

// Let `N` be `size(range)`.
//
// Preconditions: `result` is not in the range `(begin(range), end(range)]`.
//
// Effects: Copies elements in `range` into the range `[result - N, result)`
// starting from `end(range) - 1` and proceeding to `begin(range)`. For each
// positive integer `n ≤ N`, performs `*(result - n) = *(end(range) - n)`.
//
// Returns: `result - N`
//
// Complexity: Exactly `N` assignments.
//
// Reference: https://wg21.link/alg.copy#:~:text=ranges::copy_backward(I
template <typename Range,
          typename BidirectionalIterator,
          typename = internal::iterator_category_t<BidirectionalIterator>>
constexpr auto copy_backward(Range&& range, BidirectionalIterator result) {
  return ranges::copy_backward(ranges::begin(range), ranges::end(range),
                               result);
}

// [alg.move] Move
// Reference: https://wg21.link/alg.move

// TODO(crbug.com/1071094): Implement.

// [alg.swap] Swap
// Reference: https://wg21.link/alg.swap

// TODO(crbug.com/1071094): Implement.

// [alg.transform] Transform
// Reference: https://wg21.link/alg.transform

// TODO(crbug.com/1071094): Implement.

// [alg.replace] Replace
// Reference: https://wg21.link/alg.replace

// TODO(crbug.com/1071094): Implement.

// [alg.fill] Fill
// Reference: https://wg21.link/alg.fill

// TODO(crbug.com/1071094): Implement.

// [alg.generate] Generate
// Reference: https://wg21.link/alg.generate

// TODO(crbug.com/1071094): Implement.

// [alg.remove] Remove
// Reference: https://wg21.link/alg.remove

// TODO(crbug.com/1071094): Implement.

// [alg.unique] Unique
// Reference: https://wg21.link/alg.unique

// TODO(crbug.com/1071094): Implement.

// [alg.reverse] Reverse
// Reference: https://wg21.link/alg.reverse

// TODO(crbug.com/1071094): Implement.

// [alg.rotate] Rotate
// Reference: https://wg21.link/alg.rotate

// TODO(crbug.com/1071094): Implement.

// [alg.random.shuffle] Shuffle
// Reference: https://wg21.link/alg.random.shuffle

// TODO(crbug.com/1071094): Implement.

// [alg.nonmodifying] Sorting and related operations
// Reference: https://wg21.link/alg.sorting

// [alg.sort] Sorting
// Reference: https://wg21.link/alg.sort

// [sort] sort
// Reference: https://wg21.link/sort

// TODO(crbug.com/1071094): Implement.

// [stable.sort] stable_sort
// Reference: https://wg21.link/stable.sort

// TODO(crbug.com/1071094): Implement.

// [partial.sort] partial_sort
// Reference: https://wg21.link/partial.sort

// TODO(crbug.com/1071094): Implement.

// [partial.sort.copy] partial_sort_copy
// Reference: https://wg21.link/partial.sort.copy

// TODO(crbug.com/1071094): Implement.

// [is.sorted] is_sorted
// Reference: https://wg21.link/is.sorted

// TODO(crbug.com/1071094): Implement.

// [alg.nth.element] Nth element
// Reference: https://wg21.link/alg.nth.element

// TODO(crbug.com/1071094): Implement.

// [alg.binary.search] Binary search
// Reference: https://wg21.link/alg.binary.search

// [lower.bound] lower_bound
// Reference: https://wg21.link/lower.bound

// Preconditions: The elements `e` of `[first, last)` are partitioned with
// respect to the expression `bool(invoke(comp, invoke(proj, e), value))`.
//
// Returns: The furthermost iterator `i` in the range `[first, last]` such that
// for every iterator `j` in the range `[first, i)`,
// `bool(invoke(comp, invoke(proj, *j), value))` is true.
//
// Complexity: At most `log(last - first) + O(1)` comparisons and projections.
//
// Reference: https://wg21.link/lower.bound#:~:text=ranges::lower_bound(I
template <typename ForwardIterator,
          typename T,
          typename Proj = identity,
          typename Comp = ranges::less,
          typename = internal::iterator_category_t<ForwardIterator>>
constexpr auto lower_bound(ForwardIterator first,
                           ForwardIterator last,
                           const T& value,
                           Comp comp = {},
                           Proj proj = {}) {
  // The second arg is guaranteed to be `value`, so we'll simply apply the
  // identity projection.
  identity value_proj;
  return std::lower_bound(
      first, last, value,
      internal::ProjectedBinaryPredicate(comp, proj, value_proj));
}

// Preconditions: The elements `e` of `[first, last)` are partitioned with
// respect to the expression `bool(invoke(comp, invoke(proj, e), value))`.
//
// Returns: The furthermost iterator `i` in the range
// `[begin(range), end(range)]` such that for every iterator `j` in the range
// `[begin(range), i)`, `bool(invoke(comp, invoke(proj, *j), value))` is true.
//
// Complexity: At most `log(size(range)) + O(1)` comparisons and projections.
//
// Reference: https://wg21.link/lower.bound#:~:text=ranges::lower_bound(R
template <typename Range,
          typename T,
          typename Proj = identity,
          typename Comp = ranges::less>
constexpr auto lower_bound(Range&& range,
                           const T& value,
                           Comp comp = {},
                           Proj proj = {}) {
  return ranges::lower_bound(ranges::begin(range), ranges::end(range), value,
                             std::move(comp), std::move(proj));
}

// [upper.bound] upper_bound
// Reference: https://wg21.link/upper.bound

// TODO(crbug.com/1071094): Implement.

// [equal.range] equal_range
// Reference: https://wg21.link/equal.range

// TODO(crbug.com/1071094): Implement.

// [binary.search] binary_search
// Reference: https://wg21.link/binary.search

// TODO(crbug.com/1071094): Implement.

// [alg.partitions] Partitions
// Reference: https://wg21.link/alg.partitions

// TODO(crbug.com/1071094): Implement.

// [alg.merge] Merge
// Reference: https://wg21.link/alg.merge

// TODO(crbug.com/1071094): Implement.

// [alg.set.operations] Set operations on sorted structures
// Reference: https://wg21.link/alg.set.operations

// [includes] includes
// Reference: https://wg21.link/includes

// TODO(crbug.com/1071094): Implement.

// [set.union] set_union
// Reference: https://wg21.link/set.union

// TODO(crbug.com/1071094): Implement.

// [set.intersection] set_intersection
// Reference: https://wg21.link/set.intersection

// TODO(crbug.com/1071094): Implement.

// [set.difference] set_difference
// Reference: https://wg21.link/set.difference

// TODO(crbug.com/1071094): Implement.

// [set.symmetric.difference] set_symmetric_difference
// Reference: https://wg21.link/set.symmetric.difference

// TODO(crbug.com/1071094): Implement.

// [alg.heap.operations] Heap operations
// Reference: https://wg21.link/alg.heap.operations

// [push.heap] push_heap
// Reference: https://wg21.link/push.heap

// TODO(crbug.com/1071094): Implement.

// [pop.heap] pop_heap
// Reference: https://wg21.link/pop.heap

// TODO(crbug.com/1071094): Implement.

// [make.heap] make_heap
// Reference: https://wg21.link/make.heap

// TODO(crbug.com/1071094): Implement.

// [sort.heap] sort_heap
// Reference: https://wg21.link/sort.heap

// TODO(crbug.com/1071094): Implement.

// [is.heap] is_heap
// Reference: https://wg21.link/is.heap

// TODO(crbug.com/1071094): Implement.

// [alg.min.max] Minimum and maximum
// Reference: https://wg21.link/alg.min.max

// TODO(crbug.com/1071094): Implement.

// [alg.lex.comparison] Lexicographical comparison
// Reference: https://wg21.link/alg.lex.comparison

// TODO(crbug.com/1071094): Implement.

// [alg.permutation.generators] Permutation generators
// Reference: https://wg21.link/alg.permutation.generators

// TODO(crbug.com/1071094): Implement.

}  // namespace ranges

}  // namespace util

#endif  // BASE_UTIL_RANGES_ALGORITHM_H_
