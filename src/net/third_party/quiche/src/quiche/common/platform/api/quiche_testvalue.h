// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_TESTVALUE_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_TESTVALUE_H_

#include "quiche_platform_impl/quiche_testvalue_impl.h"
#include "absl/strings/string_view.h"

namespace quiche {

// Interface allowing injection of test-specific code in production codepaths.
// |label| is an arbitrary value identifying the location, and |var| is a
// pointer to the value to be modified.
//
// Note that this method does nothing in Chromium.
template <class T>
void AdjustTestValue(absl::string_view label, T* var) {
  AdjustTestValueImpl(label, var);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_TESTVALUE_H_
