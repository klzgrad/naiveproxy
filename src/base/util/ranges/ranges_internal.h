// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_UTIL_RANGES_RANGES_INTERNAL_H_
#define BASE_UTIL_RANGES_RANGES_INTERNAL_H_

#include <stddef.h>

namespace util {
namespace ranges {
namespace internal {

// Helper to express preferences in an overload set. If more than one overload
// are available for a given set of parameters the overload with the higher
// priority will be chosen.
template <size_t I>
struct priority_tag : priority_tag<I - 1> {};

template <>
struct priority_tag<0> {};

}  // namespace internal
}  // namespace ranges
}  // namespace util

#endif  // BASE_UTIL_RANGES_RANGES_INTERNAL_H_
