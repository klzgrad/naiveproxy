// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_TEXT_UTILS_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_TEXT_UTILS_H_

#include <string>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/platform/api/quiche_export.h"
#include "net/quiche/common/platform/impl/quiche_text_utils_impl.h"

namespace quiche {

// Various utilities for manipulating text.
class QUICHE_EXPORT QuicheTextUtils {
 public:
  // Returns a new string in which |data| has been converted to lower case.
  static std::string ToLower(absl::string_view data) {
    return quiche::QuicheTextUtilsImpl::ToLower(data);
  }

  // Removes leading and trailing whitespace from |data|.
  static void RemoveLeadingAndTrailingWhitespace(absl::string_view* data) {
    quiche::QuicheTextUtilsImpl::RemoveLeadingAndTrailingWhitespace(data);
  }

  // Returns a new string representing |in|.
  static std::string Uint64ToString(uint64_t in) {
    return quiche::QuicheTextUtilsImpl::Uint64ToString(in);
  }

  // This converts a uint32 into an 8-character hexidecimal
  // representation.  Return value: 8 characters of ASCII string.
  static std::string Hex(uint32_t v) {
    return quiche::QuicheTextUtilsImpl::Hex(v);
  }

  // Base64 encodes with no padding |data_len| bytes of |data| into |output|.
  static void Base64Encode(const uint8_t* data,
                           size_t data_len,
                           std::string* output) {
    return quiche::QuicheTextUtilsImpl::Base64Encode(data, data_len, output);
  }

  // Decodes a base64-encoded |input|.  Returns nullopt when the input is
  // invalid.
  static absl::optional<std::string> Base64Decode(absl::string_view input) {
    return quiche::QuicheTextUtilsImpl::Base64Decode(input);
  }

  // Returns a string containing hex and ASCII representations of |binary|,
  // side-by-side in the style of hexdump. Non-printable characters will be
  // printed as '.' in the ASCII output.
  // For example, given the input "Hello, QUIC!\01\02\03\04", returns:
  // "0x0000:  4865 6c6c 6f2c 2051 5549 4321 0102 0304  Hello,.QUIC!...."
  static std::string HexDump(absl::string_view binary_data) {
    return quiche::QuicheTextUtilsImpl::HexDump(binary_data);
  }

  // Returns true if |data| contains any uppercase characters.
  static bool ContainsUpperCase(absl::string_view data) {
    return quiche::QuicheTextUtilsImpl::ContainsUpperCase(data);
  }

  // Returns true if |data| contains only decimal digits.
  static bool IsAllDigits(absl::string_view data) {
    return quiche::QuicheTextUtilsImpl::IsAllDigits(data);
  }
};

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_TEXT_UTILS_H_
