// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_TEST_TOOLS_SPDY_TEST_UTILS_H_
#define QUICHE_HTTP2_TEST_TOOLS_SPDY_TEST_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/http2/core/spdy_protocol.h"
#include "quiche/common/http/http_header_block.h"

// TODO(b/318698478): update the namespace and file name
namespace spdy {

inline bool operator==(absl::string_view x,
                       const quiche::HttpHeaderBlock::ValueProxy& y) {
  return y.operator==(x);
}

namespace test {

std::string HexDumpWithMarks(const unsigned char* data, int length,
                             const bool* marks, int mark_length);

void CompareCharArraysWithHexError(const std::string& description,
                                   const unsigned char* actual,
                                   const int actual_len,
                                   const unsigned char* expected,
                                   const int expected_len);

void SetFrameFlags(SpdySerializedFrame* frame, uint8_t flags);

void SetFrameLength(SpdySerializedFrame* frame, size_t length);

// Makes a SpdySerializedFrame by copying the memory identified by `data` and
// `length`.
SpdySerializedFrame MakeSerializedFrame(const char* data, size_t length);

}  // namespace test
}  // namespace spdy

#endif  // QUICHE_HTTP2_TEST_TOOLS_SPDY_TEST_UTILS_H_
