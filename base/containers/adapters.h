// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CONTAINERS_ADAPTERS_H_
#define BASE_CONTAINERS_ADAPTERS_H_

#include <stddef.h>

#include <iterator>

#include "base/macros.h"

namespace base {

namespace internal {

// Internal adapter class for implementing base::Reversed.
template <typename T>
class ReversedAdapter {
 public:
  using Iterator = decltype(static_cast<T*>(nullptr)->rbegin());

  explicit ReversedAdapter(T& t) : t_(t) {}
  ReversedAdapter(const ReversedAdapter& ra) : t_(ra.t_) {}

  // TODO(mdempsky): Once we can use C++14 library features, use std::rbegin
  // and std::rend instead, so we can remove the specialization below.
  Iterator begin() const { return t_.rbegin(); }
  Iterator end() const { return t_.rend(); }

 private:
  T& t_;

  DISALLOW_ASSIGN(ReversedAdapter);
};

template <typename T, size_t N>
class ReversedAdapter<T[N]> {
 public:
  using Iterator = std::reverse_iterator<T*>;

  explicit ReversedAdapter(T (&t)[N]) : t_(t) {}
  ReversedAdapter(const ReversedAdapter& ra) : t_(ra.t_) {}

  Iterator begin() const { return Iterator(&t_[N]); }
  Iterator end() const { return Iterator(&t_[0]); }

 private:
  T (&t_)[N];

  DISALLOW_ASSIGN(ReversedAdapter);
};

}  // namespace internal

// Reversed returns a container adapter usable in a range-based "for" statement
// for iterating a reversible container in reverse order.
//
// Example:
//
//   std::vector<int> v = ...;
//   for (int i : base::Reversed(v)) {
//     // iterates through v from back to front
//   }
template <typename T>
internal::ReversedAdapter<T> Reversed(T& t) {
  return internal::ReversedAdapter<T>(t);
}

}  // namespace base

#endif  // BASE_CONTAINERS_ADAPTERS_H_
