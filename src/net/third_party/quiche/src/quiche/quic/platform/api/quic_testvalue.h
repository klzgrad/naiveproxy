// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(b/178613777): Remove this file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_TESTVALUE_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_TESTVALUE_H_

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_testvalue.h"

namespace quic {

template <class T>
void AdjustTestValue(absl::string_view label, T* var) {
  quiche::AdjustTestValue(label, var);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_TESTVALUE_H_
