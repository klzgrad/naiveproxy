// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a "No Compile Test".
// http://dev.chromium.org/developers/testing/no-compile-tests

#include "base/strings/string16.h"

#if defined(NCTEST_NO_KOENIG_LOOKUP_FOR_STRING16)  // [r"use of undeclared identifier 'ShouldNotBeFound'"]

// base::string16 is declared as a typedef. It should not cause other functions
// in base to be found via Argument-dependent lookup.

namespace base {
void ShouldNotBeFound(const base::string16& arg) {}
}

// Intentionally not in base:: namespace.
void WontCompile() {
  base::string16 s;
  ShouldNotBeFound(s);
}

#endif
