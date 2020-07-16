// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODING_ERROR_H_
#define QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODING_ERROR_H_

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace http2 {

enum class HpackDecodingError {
  // No error detected so far.
  kOk,
  // Varint beyond implementation limit.
  kIndexVarintError,
  kNameLengthVarintError,
  kValueLengthVarintError,
  // String literal length exceeds buffer limit.
  kNameTooLong,
  kValueTooLong,
  // Error in Huffman encoding.
  kNameHuffmanError,
  kValueHuffmanError,
  // Next instruction should have been a dynamic table size update.
  kMissingDynamicTableSizeUpdate,
  // Invalid index in indexed header field representation.
  kInvalidIndex,
  // Invalid index in literal header field with indexed name representation.
  kInvalidNameIndex,
  // Dynamic table size update not allowed.
  kDynamicTableSizeUpdateNotAllowed,
  // Initial dynamic table size update is above low water mark.
  kInitialDynamicTableSizeUpdateIsAboveLowWaterMark,
  // Dynamic table size update is above acknowledged setting.
  kDynamicTableSizeUpdateIsAboveAcknowledgedSetting,
  // HPACK block ends in the middle of an instruction.
  kTruncatedBlock,
  // Incoming data fragment exceeds buffer limit.
  kFragmentTooLong,
  // Total compressed HPACK data size exceeds limit.
  kCompressedHeaderSizeExceedsLimit,
};

QUICHE_EXPORT_PRIVATE quiche::QuicheStringPiece HpackDecodingErrorToString(
    HpackDecodingError error);

}  // namespace http2

#endif  // QUICHE_HTTP2_HPACK_DECODER_HPACK_DECODING_ERROR_H_
