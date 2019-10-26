// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_PLATFORM_API_HTTP2_STRING_UTILS_H_
#define QUICHE_HTTP2_PLATFORM_API_HTTP2_STRING_UTILS_H_

#include <string>
#include <type_traits>
#include <utility>

#include "net/third_party/quiche/src/http2/platform/api/http2_string_piece.h"
#include "net/http2/platform/impl/http2_string_utils_impl.h"

namespace http2 {

template <typename... Args>
inline std::string Http2StrCat(const Args&... args) {
  return Http2StrCatImpl(std::forward<const Args&>(args)...);
}

template <typename... Args>
inline void Http2StrAppend(std::string* output, const Args&... args) {
  Http2StrAppendImpl(output, std::forward<const Args&>(args)...);
}

template <typename... Args>
inline std::string Http2StringPrintf(const Args&... args) {
  return Http2StringPrintfImpl(std::forward<const Args&>(args)...);
}

inline std::string Http2HexEncode(const void* bytes, size_t size) {
  return Http2HexEncodeImpl(bytes, size);
}

inline std::string Http2HexDecode(Http2StringPiece data) {
  return Http2HexDecodeImpl(data);
}

inline std::string Http2HexDump(Http2StringPiece data) {
  return Http2HexDumpImpl(data);
}

inline std::string Http2HexEscape(Http2StringPiece data) {
  return Http2HexEscapeImpl(data);
}

template <typename Number>
inline std::string Http2Hex(Number number) {
  static_assert(std::is_integral<Number>::value, "Number has to be an int");
  return Http2HexImpl(number);
}

}  // namespace http2

#endif  // QUICHE_HTTP2_PLATFORM_API_HTTP2_STRING_UTILS_H_
