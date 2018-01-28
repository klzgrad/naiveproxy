// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_PLATFORM_API_HTTP2_STRING_UTILS_H_
#define NET_HTTP2_PLATFORM_API_HTTP2_STRING_UTILS_H_

#include <utility>

#include "net/http2/platform/api/http2_string.h"
#include "net/http2/platform/api/http2_string_piece.h"
#include "net/http2/platform/impl/http2_string_utils_impl.h"

namespace net {

template <typename... Args>
inline Http2String Http2StrCat(const Args&... args) {
  return Http2StrCatImpl(std::forward<const Args&>(args)...);
}

template <typename... Args>
inline void Http2StrAppend(Http2String* output, const Args&... args) {
  Http2StrAppendImpl(output, std::forward<const Args&>(args)...);
}

template <typename... Args>
inline Http2String Http2StringPrintf(const Args&... args) {
  return Http2StringPrintfImpl(std::forward<const Args&>(args)...);
}

inline Http2String Http2HexEncode(const void* bytes, size_t size) {
  return Http2HexEncodeImpl(bytes, size);
}

inline Http2String Http2HexDecode(Http2StringPiece data) {
  return Http2HexDecodeImpl(data);
}

inline Http2String Http2HexDump(Http2StringPiece data) {
  return Http2HexDumpImpl(data);
}

}  // namespace net

#endif  // NET_HTTP2_PLATFORM_API_HTTP2_STRING_UTILS_H_
