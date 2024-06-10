// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_TEST_TOOLS_SPDY_TEST_UTILS_H_
#define QUICHE_SPDY_TEST_TOOLS_SPDY_TEST_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace spdy {

inline bool operator==(absl::string_view x,
                       const Http2HeaderBlock::ValueProxy& y) {
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

#endif  // QUICHE_SPDY_TEST_TOOLS_SPDY_TEST_UTILS_H_
