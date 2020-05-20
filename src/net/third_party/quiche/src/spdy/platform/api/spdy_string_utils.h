// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_STRING_UTILS_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_STRING_UTILS_H_

#include <string>
#include <utility>

// The following header file has to be included from at least
// non-test file in order to avoid strange linking errors.
// TODO(bnc): Remove this include as soon as it is included elsewhere in
// non-test code.
#include "net/third_party/quiche/src/spdy/platform/api/spdy_mem_slice.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/spdy/platform/impl/spdy_string_utils_impl.h"

namespace spdy {

template <typename... Args>
inline void SpdyStrAppend(std::string* output, const Args&... args) {
  SpdyStrAppendImpl(output, std::forward<const Args&>(args)...);
}

inline char SpdyHexDigitToInt(char c) {
  return SpdyHexDigitToIntImpl(c);
}

inline std::string SpdyHexDecode(quiche::QuicheStringPiece data) {
  return SpdyHexDecodeImpl(data);
}

inline bool SpdyHexDecodeToUInt32(quiche::QuicheStringPiece data,
                                  uint32_t* out) {
  return SpdyHexDecodeToUInt32Impl(data, out);
}

inline std::string SpdyHexEncode(const char* bytes, size_t size) {
  return SpdyHexEncodeImpl(bytes, size);
}

inline std::string SpdyHexEncodeUInt32AndTrim(uint32_t data) {
  return SpdyHexEncodeUInt32AndTrimImpl(data);
}

inline std::string SpdyHexDump(quiche::QuicheStringPiece data) {
  return SpdyHexDumpImpl(data);
}

using SpdyStringPieceCaseHash = SpdyStringPieceCaseHashImpl;

using SpdyStringPieceCaseEq = SpdyStringPieceCaseEqImpl;

}  // namespace spdy

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_STRING_UTILS_H_
