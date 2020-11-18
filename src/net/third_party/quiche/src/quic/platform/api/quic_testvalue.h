// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_TESTVALUE_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_TESTVALUE_H_

#include "net/quic/platform/impl/quic_testvalue_impl.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// Interface allowing injection of test-specific code in production codepaths.
// |label| is an arbitrary value identifying the location, and |var| is a
// pointer to the value to be modified.
//
// Note that this method does nothing in Chromium.
template <class T>
void AdjustTestValue(quiche::QuicheStringPiece label, T* var) {
  AdjustTestValueImpl(label, var);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_TESTVALUE_H_
