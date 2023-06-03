// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_HUFFMAN_HUFFMAN_SPEC_TABLES_H_
#define QUICHE_HTTP2_HPACK_HUFFMAN_HUFFMAN_SPEC_TABLES_H_

// Tables describing the Huffman encoding of bytes as specified by RFC7541.

#include <cstdint>

#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {

struct QUICHE_EXPORT HuffmanSpecTables {
  // Number of bits in the encoding of each symbol (byte).
  static const uint8_t kCodeLengths[257];

  // The encoding of each symbol, right justified (as printed), which means that
  // the last bit of the encoding is the low-order bit of the uint32.
  static const uint32_t kRightCodes[257];

  // The encoding of each symbol, left justified (as printed), which means that
  // the first bit of the encoding is the high-order bit of the uint32.
  static const uint32_t kLeftCodes[257];
};

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_HUFFMAN_HUFFMAN_SPEC_TABLES_H_
