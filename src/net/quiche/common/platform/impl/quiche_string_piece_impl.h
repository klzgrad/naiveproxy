// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_STRING_PIECE_IMPL_H_
#define NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_STRING_PIECE_IMPL_H_

#include "base/strings/string_piece.h"

namespace quiche {

using QuicheStringPieceImpl = base::StringPiece;

using QuicheStringPieceHashImpl = base::StringPieceHash;

inline size_t QuicheHashStringPairImpl(QuicheStringPieceImpl a,
                                       QuicheStringPieceImpl b) {
  return base::StringPieceHash()(a) ^ base::StringPieceHash()(b);
}

}  // namespace quiche

#endif  // NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_STRING_PIECE_IMPL_H_
