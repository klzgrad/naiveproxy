// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a "No Compile Test" suite.
// http://dev.chromium.org/developers/testing/no-compile-tests

#include "base/containers/unique_any.h"
#include "base/test/move_only_int.h"

namespace base {

#if defined(NCTEST_UNIQUE_ANY_CANT_COPY_CONSTRUCT)  // [r"fatal error: call to implicitly-deleted copy constructor of 'base::unique_any'"]

void WontCompile() {
  unique_any a(1);
  unique_any b(a);
}

#elif defined(NCTEST_UNIQUE_ANY_CANT_COPY_ASSIGN)  // [r"fatal error: object of type 'base::unique_any' cannot be assigned because its copy assignment operator is implicitly deleted"]

void WontCompile() {
  unique_any a;
  unique_any b(1);
  a = b;
}

#elif defined(NCTEST_UNIQUE_ANY_CAST_WITH_NON_REFERENCE_TYPE)  // [r"fatal error: static_assert failed due to requirement 'std::is_constructible.*\"Invalid ValueType\""]

void WontCompile() {
  unique_any a(MoveOnlyInt{});

  unique_any_cast<MoveOnlyInt>(a);  // Fails because this would return a copy.
}

#endif

}  // namespace base
