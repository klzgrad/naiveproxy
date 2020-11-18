// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/spdy_test_utils.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_endianness_util.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_logging.h"

namespace spdy {
namespace test {

std::string HexDumpWithMarks(const unsigned char* data,
                             int length,
                             const bool* marks,
                             int mark_length) {
  static const char kHexChars[] = "0123456789abcdef";
  static const int kColumns = 4;

  const int kSizeLimit = 1024;
  if (length > kSizeLimit || mark_length > kSizeLimit) {
    SPDY_LOG(ERROR) << "Only dumping first " << kSizeLimit << " bytes.";
    length = std::min(length, kSizeLimit);
    mark_length = std::min(mark_length, kSizeLimit);
  }

  std::string hex;
  for (const unsigned char* row = data; length > 0;
       row += kColumns, length -= kColumns) {
    for (const unsigned char* p = row; p < row + 4; ++p) {
      if (p < row + length) {
        const bool mark =
            (marks && (p - data) < mark_length && marks[p - data]);
        hex += mark ? '*' : ' ';
        hex += kHexChars[(*p & 0xf0) >> 4];
        hex += kHexChars[*p & 0x0f];
        hex += mark ? '*' : ' ';
      } else {
        hex += "    ";
      }
    }
    hex = hex + "  ";

    for (const unsigned char* p = row; p < row + 4 && p < row + length; ++p) {
      hex += (*p >= 0x20 && *p <= 0x7f) ? (*p) : '.';
    }

    hex = hex + '\n';
  }
  return hex;
}

void CompareCharArraysWithHexError(const std::string& description,
                                   const unsigned char* actual,
                                   const int actual_len,
                                   const unsigned char* expected,
                                   const int expected_len) {
  const int min_len = std::min(actual_len, expected_len);
  const int max_len = std::max(actual_len, expected_len);
  std::unique_ptr<bool[]> marks(new bool[max_len]);
  bool identical = (actual_len == expected_len);
  for (int i = 0; i < min_len; ++i) {
    if (actual[i] != expected[i]) {
      marks[i] = true;
      identical = false;
    } else {
      marks[i] = false;
    }
  }
  for (int i = min_len; i < max_len; ++i) {
    marks[i] = true;
  }
  if (identical)
    return;
  ADD_FAILURE() << "Description:\n"
                << description << "\n\nExpected:\n"
                << HexDumpWithMarks(expected, expected_len, marks.get(),
                                    max_len)
                << "\nActual:\n"
                << HexDumpWithMarks(actual, actual_len, marks.get(), max_len);
}

void SetFrameFlags(SpdySerializedFrame* frame, uint8_t flags) {
  frame->data()[4] = flags;
}

void SetFrameLength(SpdySerializedFrame* frame, size_t length) {
  CHECK_GT(1u << 14, length);
  {
    int32_t wire_length = SpdyHostToNet32(length);
    memcpy(frame->data(), reinterpret_cast<char*>(&wire_length) + 1, 3);
  }
}

void TestHeadersHandler::OnHeaderBlockStart() {
  block_.clear();
}

void TestHeadersHandler::OnHeader(quiche::QuicheStringPiece name,
                                  quiche::QuicheStringPiece value) {
  block_.AppendValueOrAddHeader(name, value);
}

void TestHeadersHandler::OnHeaderBlockEnd(
    size_t header_bytes_parsed,
    size_t compressed_header_bytes_parsed) {
  header_bytes_parsed_ = header_bytes_parsed;
  compressed_header_bytes_parsed_ = compressed_header_bytes_parsed;
}

}  // namespace test
}  // namespace spdy
