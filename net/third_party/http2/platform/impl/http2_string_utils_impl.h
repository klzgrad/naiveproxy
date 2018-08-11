// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_HTTP2_STRING_UTILS_IMPL_H_
#define NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_HTTP2_STRING_UTILS_IMPL_H_

#include <sstream>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "net/base/hex_utils.h"
#include "net/third_party/http2/platform/api/http2_export.h"
#include "net/third_party/http2/platform/api/http2_string.h"
#include "net/third_party/http2/platform/api/http2_string_piece.h"

namespace http2 {

template <typename... Args>
inline Http2String Http2StrCatImpl(const Args&... args) {
  std::ostringstream oss;
  int dummy[] = {1, (oss << args, 0)...};
  static_cast<void>(dummy);
  return oss.str();
}

template <typename... Args>
inline void Http2StrAppendImpl(Http2String* output, Args... args) {
  output->append(Http2StrCatImpl(args...));
}

template <typename... Args>
inline Http2String Http2StringPrintfImpl(const Args&... args) {
  return base::StringPrintf(std::forward<const Args&>(args)...);
}

inline Http2String Http2HexEncodeImpl(const void* bytes, size_t size) {
  return base::HexEncode(bytes, size);
}

inline Http2String Http2HexDecodeImpl(Http2StringPiece data) {
  return net::HexDecode(data);
}

inline Http2String Http2HexDumpImpl(Http2StringPiece data) {
  return net::HexDump(data);
}

}  // namespace http2

#endif  // NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_HTTP2_STRING_UTILS_IMPL_H_
