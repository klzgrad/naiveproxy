// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a "No Compile Test" suite.
// http://dev.chromium.org/developers/testing/no-compile-tests

#include <type_traits>

#include "base/numerics/safe_conversions.h"

namespace base {

void StrictCastToNonSubsuming() {
  // Should not be able to `strict_cast` to a non-subsuming type.
  [[maybe_unused]] const auto a = strict_cast<int>(size_t{0});  // expected-error {{no matching function for call to 'strict_cast'}}
  [[maybe_unused]] const auto b =
      strict_cast<int>(std::integral_constant<size_t, 0>());    // expected-error {{no matching function for call to 'strict_cast'}}
  [[maybe_unused]] const auto c = strict_cast<size_t>(0);       // expected-error {{no matching function for call to 'strict_cast'}}
}

void StrictNumericConstruction() {
  // Should not be able to construct `StrictNumeric` from a non-subsuming type.
  [[maybe_unused]] StrictNumeric<size_t> a(1);                                 // expected-error@*:* {{no matching function for call to 'strict_cast'}}
  [[maybe_unused]] StrictNumeric<size_t> b{std::integral_constant<int, 1>()};  // expected-error@*:* {{no matching function for call to 'strict_cast'}}
  [[maybe_unused]] StrictNumeric<int> c(size_t{1});                            // expected-error@*:* {{no matching function for call to 'strict_cast'}}
  [[maybe_unused]] StrictNumeric<int> d(1.0f);                                 // expected-error@*:* {{no matching function for call to 'strict_cast'}}

  // Can't use `std::integral_constant<non-integral T>` to construct
  // `StrictNumeric`.
  //
  // TODO(pkasting): There's no particular reason we couldn't support this.
  [[maybe_unused]] StrictNumeric<float> e{
      std::integral_constant<float, 1.0f>()};  // expected-error@*:* {{no matching function for call to 'strict_cast'}}
}

}  // namespace base
