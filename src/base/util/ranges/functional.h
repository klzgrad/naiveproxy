// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_UTIL_RANGES_FUNCTIONAL_H_
#define BASE_UTIL_RANGES_FUNCTIONAL_H_

#include <functional>
#include <type_traits>
#include <utility>

namespace util {

// Implementation of C++20's std::identity.
//
// Reference:
// - https://en.cppreference.com/w/cpp/utility/functional/identity
// - https://wg21.link/func.identity
struct identity {
  template <typename T>
  constexpr T&& operator()(T&& t) const noexcept {
    return std::forward<T>(t);
  }

  using is_transparent = void;
};

// Minimal implementation of C++17's std::invoke. Based on implementation
// referenced in original std::invoke proposal.
//
// Note: Unlike C++20's std::invoke this implementation is not constexpr. A
// constexpr version can be added in the future, but it won't be as concise,
// since std::mem_fn is not constexpr prior to C++20.
//
// References:
// - https://wg21.link/n4169#implementability
// - https://en.cppreference.com/w/cpp/utility/functional/invoke
// - https://wg21.link/func.invoke
template <typename Functor,
          typename... Args,
          std::enable_if_t<
              std::is_member_pointer<std::decay_t<Functor>>::value>* = nullptr>
decltype(auto) invoke(Functor&& f, Args&&... args) {
  return std::mem_fn(f)(std::forward<Args>(args)...);
}

template <typename Functor,
          typename... Args,
          std::enable_if_t<
              !std::is_member_pointer<std::decay_t<Functor>>::value>* = nullptr>
decltype(auto) invoke(Functor&& f, Args&&... args) {
  return std::forward<Functor>(f)(std::forward<Args>(args)...);
}

// Simplified implementations of C++20's std::ranges comparison function
// objects. As opposed to the std::ranges implementation, these versions do not
// constrain the passed-in types.
//
// Reference: https://wg21.link/range.cmp
namespace ranges {
using equal_to = std::equal_to<>;
using not_equal_to = std::not_equal_to<>;
using greater = std::greater<>;
using less = std::less<>;
using greater_equal = std::greater_equal<>;
using less_equal = std::less_equal<>;
}  // namespace ranges

}  // namespace util

#endif  // BASE_UTIL_RANGES_FUNCTIONAL_H_
