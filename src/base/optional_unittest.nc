// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a "No Compile Test" suite.
// http://dev.chromium.org/developers/testing/no-compile-tests

#include <type_traits>

#include "base/optional.h"

namespace base {

#if defined(NCTEST_EXPLICIT_CONVERTING_COPY_CONSTRUCTOR)  // [r"fatal error: no matching function for call to object of type"]

// Optional<T>(const Optional<U>& arg) constructor is marked explicit if
// T is not convertible from "const U&".
void WontCompile() {
  struct Test {
    // Declares as explicit so that Test is still constructible from int,
    // but not convertible.
    explicit Test(int a) {}
  };

  static_assert(!std::is_convertible<const int&, Test>::value,
                "const int& to Test is convertible");
  const Optional<int> arg(in_place, 1);
  ([](Optional<Test> param) {})(arg);
}

#elif defined(NCTEST_EXPLICIT_CONVERTING_MOVE_CONSTRUCTOR)  // [r"fatal error: no matching function for call to object of type"]

// Optional<T>(Optional<U>&& arg) constructor is marked explicit if
// T is not convertible from "U&&".
void WontCompile() {
  struct Test {
    // Declares as explicit so that Test is still constructible from int,
    // but not convertible.
    explicit Test(int a) {}
  };

  static_assert(!std::is_convertible<int&&, Test>::value,
                "int&& to Test is convertible");
  ([](Optional<Test> param) {})(Optional<int>(in_place, 1));
}

#elif defined(NCTEST_EXPLICIT_VALUE_FORWARD_CONSTRUCTOR)  // [r"fatal error: no matching function for call to object of type"]

// Optional<T>(U&&) constructor is marked explicit if T is not convertible
// from U&&.
void WontCompile() {
  struct Test {
    // Declares as explicit so that Test is still constructible from int,
    // but not convertible.
    explicit Test(int a) {}
  };

  static_assert(!std::is_convertible<int&&, Test>::value,
                "int&& to Test is convertible");
  ([](Optional<Test> param) {})(1);
}

#endif

}  // namespace base
