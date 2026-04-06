// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_TEXT_UTILS_H_
#define QUICHE_COMMON_QUICHE_TEXT_UTILS_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "absl/base/optimization.h"
#include "absl/container/fixed_array.h"
#include "absl/hash/hash.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

struct QUICHE_EXPORT StringPieceCaseHash {
  size_t operator()(absl::string_view data) const {
    // The longest request header name currently in Chromium source is 37
    // characters. The longest response header is 35 characters. We'd like to
    // size our inline storage to be a multiple of a cache line but not less
    // than 37.
    constexpr size_t kLongestExpectedHeaderName = 37;
    constexpr size_t kCacheLineSize = ABSL_CACHELINE_SIZE;
    constexpr size_t kInlineStorage =
        ((kLongestExpectedHeaderName + kCacheLineSize - 1) / kCacheLineSize) *
        kCacheLineSize;
    // This implementation of ascii_tolower is functionally equivalent to
    // absl::ascii_tolower but is easier for the compiler to vectorize.
    constexpr auto ascii_tolower = [](char c) {
      return (c >= 'A' && c <= 'Z') ? c | 32 : c;
    };
    ABSL_CACHELINE_ALIGNED absl::FixedArray<char, kInlineStorage> lower(
        data.size());
    std::transform(data.begin(), data.end(), lower.begin(), ascii_tolower);
    return absl::HashOf(lower);
  }
};

struct QUICHE_EXPORT StringPieceCaseEqual {
  bool operator()(absl::string_view piece1, absl::string_view piece2) const {
    return absl::EqualsIgnoreCase(piece1, piece2);
  }
};

// Various utilities for manipulating text.
class QUICHE_EXPORT QuicheTextUtils {
 public:
  // Returns a new string in which |data| has been converted to lower case.
  static std::string ToLower(absl::string_view data) {
    return absl::AsciiStrToLower(data);
  }

  // Removes leading and trailing whitespace from |data|.
  static void RemoveLeadingAndTrailingWhitespace(absl::string_view* data) {
    *data = absl::StripAsciiWhitespace(*data);
  }

  // Base64 encodes with no padding |data_len| bytes of |data| into |output|.
  static void Base64Encode(const uint8_t* data, size_t data_len,
                           std::string* output);

  // Decodes a base64-encoded |input|.  Returns nullopt when the input is
  // invalid.
  static std::optional<std::string> Base64Decode(absl::string_view input);

  // Returns a string containing hex and ASCII representations of |binary|,
  // side-by-side in the style of hexdump. Non-printable characters will be
  // printed as '.' in the ASCII output.
  // For example, given the input "Hello, QUIC!\01\02\03\04", returns:
  // "0x0000:  4865 6c6c 6f2c 2051 5549 4321 0102 0304  Hello,.QUIC!...."
  static std::string HexDump(absl::string_view binary_data);

  // Returns true if |data| contains any uppercase characters.
  static bool ContainsUpperCase(absl::string_view data) {
    return std::any_of(data.begin(), data.end(), absl::ascii_isupper);
  }

  // Returns true if |data| contains only decimal digits.
  static bool IsAllDigits(absl::string_view data) {
    return std::all_of(data.begin(), data.end(), absl::ascii_isdigit);
  }
};

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_TEXT_UTILS_H_
