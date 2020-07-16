// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoding_error.h"

namespace http2 {

// static
quiche::QuicheStringPiece HpackDecodingErrorToString(HpackDecodingError error) {
  switch (error) {
    case HpackDecodingError::kOk:
      return "No error detected";
    case HpackDecodingError::kIndexVarintError:
      return "Index varint beyond implementation limit";
    case HpackDecodingError::kNameLengthVarintError:
      return "Name length varint beyond implementation limit";
    case HpackDecodingError::kValueLengthVarintError:
      return "Value length varint beyond implementation limit";
    case HpackDecodingError::kNameTooLong:
      return "Name length exceeds buffer limit";
    case HpackDecodingError::kValueTooLong:
      return "Value length exceeds buffer limit";
    case HpackDecodingError::kNameHuffmanError:
      return "Name Huffman encoding error";
    case HpackDecodingError::kValueHuffmanError:
      return "Value Huffman encoding error";
    case HpackDecodingError::kMissingDynamicTableSizeUpdate:
      return "Missing dynamic table size update";
    case HpackDecodingError::kInvalidIndex:
      return "Invalid index in indexed header field representation";
    case HpackDecodingError::kInvalidNameIndex:
      return "Invalid index in literal header field with indexed name "
             "representation";
    case HpackDecodingError::kDynamicTableSizeUpdateNotAllowed:
      return "Dynamic table size update not allowed";
    case HpackDecodingError::kInitialDynamicTableSizeUpdateIsAboveLowWaterMark:
      return "Initial dynamic table size update is above low water mark";
    case HpackDecodingError::kDynamicTableSizeUpdateIsAboveAcknowledgedSetting:
      return "Dynamic table size update is above acknowledged setting";
    case HpackDecodingError::kTruncatedBlock:
      return "Block ends in the middle of an instruction";
    case HpackDecodingError::kFragmentTooLong:
      return "Incoming data fragment exceeds buffer limit";
    case HpackDecodingError::kCompressedHeaderSizeExceedsLimit:
      return "Total compressed HPACK data size exceeds limit";
  }
  return "invalid HpackDecodingError value";
}

}  // namespace http2
