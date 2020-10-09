// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_string_decoder.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace http2 {

std::string HpackStringDecoder::DebugString() const {
  return quiche::QuicheStrCat(
      "HpackStringDecoder(state=", StateToString(state_),
      ", length=", length_decoder_.DebugString(), ", remaining=", remaining_,
      ", huffman=", huffman_encoded_ ? "true)" : "false)");
}

// static
std::string HpackStringDecoder::StateToString(StringDecoderState v) {
  switch (v) {
    case kStartDecodingLength:
      return "kStartDecodingLength";
    case kDecodingString:
      return "kDecodingString";
    case kResumeDecodingLength:
      return "kResumeDecodingLength";
  }
  return quiche::QuicheStrCat("UNKNOWN_STATE(", static_cast<uint32_t>(v), ")");
}

std::ostream& operator<<(std::ostream& out, const HpackStringDecoder& v) {
  return out << v.DebugString();
}

}  // namespace http2
