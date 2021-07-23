// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_TESTVALUE_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_TESTVALUE_H_

#include "absl/strings/string_view.h"

// TODO(b/178613777): move into the common QUICHE platform.
#include "quiche_platform_impl/quic_testvalue_impl.h"

namespace quic {

// Interface allowing injection of test-specific code in production codepaths.
// |label| is an arbitrary value identifying the location, and |var| is a
// pointer to the value to be modified.
//
// Note that this method does nothing in Chromium.
template <class T>
void AdjustTestValue(absl::string_view label, T* var) {
  AdjustTestValueImpl(label, var);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_TESTVALUE_H_
