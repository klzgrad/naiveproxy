// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_TEXT_UTILS_H_
#define NET_QUIC_PLATFORM_API_QUIC_TEXT_UTILS_H_

#include "net/quic/platform/api/quic_string_piece.h"
#include "net/quic/platform/impl/quic_text_utils_impl.h"

namespace net {

// Various utilities for manipulating text.
class QuicTextUtils {
 public:
  // Returns true if |data| starts with |prefix|, case sensitively.
  static bool StartsWith(QuicStringPiece data, QuicStringPiece prefix) {
    return QuicTextUtilsImpl::StartsWith(data, prefix);
  }

  // Returns true if |data| ends with |suffix|, case insensitively.
  static bool EndsWithIgnoreCase(QuicStringPiece data, QuicStringPiece suffix) {
    return QuicTextUtilsImpl::EndsWithIgnoreCase(data, suffix);
  }

  // Returns a new string in which |data| has been converted to lower case.
  static std::string ToLower(QuicStringPiece data) {
    return QuicTextUtilsImpl::ToLower(data);
  }

  // Removes leading and trailing whitespace from |data|.
  static void RemoveLeadingAndTrailingWhitespace(QuicStringPiece* data) {
    QuicTextUtilsImpl::RemoveLeadingAndTrailingWhitespace(data);
  }

  // Returns true if |in| represents a valid uint64, and stores that value in
  // |out|.
  static bool StringToUint64(QuicStringPiece in, uint64_t* out) {
    return QuicTextUtilsImpl::StringToUint64(in, out);
  }

  // Returns true if |in| represents a valid int, and stores that value in
  // |out|.
  static bool StringToInt(QuicStringPiece in, int* out) {
    return QuicTextUtilsImpl::StringToInt(in, out);
  }

  // Returns true if |in| represents a valid uint32, and stores that value in
  // |out|.
  static bool StringToUint32(QuicStringPiece in, uint32_t* out) {
    return QuicTextUtilsImpl::StringToUint32(in, out);
  }

  // Returns true if |in| represents a valid size_t, and stores that value in
  // |out|.
  static bool StringToSizeT(QuicStringPiece in, size_t* out) {
    return QuicTextUtilsImpl::StringToSizeT(in, out);
  }

  // Returns a new string representing |in|.
  static std::string Uint64ToString(uint64_t in) {
    return QuicTextUtilsImpl::Uint64ToString(in);
  }

  // This converts |length| bytes of binary to a 2*|length|-character
  // hexadecimal representation.
  // Return value: 2*|length| characters of ASCII string.
  static std::string HexEncode(const char* data, size_t length) {
    return HexEncode(QuicStringPiece(data, length));
  }

  // This converts |data.length()| bytes of binary to a
  // 2*|data.length()|-character hexadecimal representation.
  // Return value: 2*|data.length()| characters of ASCII string.
  static std::string HexEncode(QuicStringPiece data) {
    return QuicTextUtilsImpl::HexEncode(data);
  }

  // This converts a uint32 into an 8-character hexidecimal
  // representation.  Return value: 8 characters of ASCII string.
  static std::string Hex(uint32_t v) { return QuicTextUtilsImpl::Hex(v); }

  // Converts |data| from a hexadecimal ASCII string to a binary string
  // that is |data.length()/2| bytes long.
  static std::string HexDecode(QuicStringPiece data) {
    return QuicTextUtilsImpl::HexDecode(data);
  }

  // Base64 encodes with no padding |data_len| bytes of |data| into |output|.
  static void Base64Encode(const uint8_t* data,
                           size_t data_len,
                           std::string* output) {
    return QuicTextUtilsImpl::Base64Encode(data, data_len, output);
  }

  // Returns a string containing hex and ASCII representations of |binary|,
  // side-by-side in the style of hexdump. Non-printable characters will be
  // printed as '.' in the ASCII output.
  // For example, given the input "Hello, QUIC!\01\02\03\04", returns:
  // "0x0000:  4865 6c6c 6f2c 2051 5549 4321 0102 0304  Hello,.QUIC!...."
  static std::string HexDump(QuicStringPiece binary_data) {
    return QuicTextUtilsImpl::HexDump(binary_data);
  }

  // Returns true if |data| contains any uppercase characters.
  static bool ContainsUpperCase(QuicStringPiece data) {
    return QuicTextUtilsImpl::ContainsUpperCase(data);
  }

  // Splits |data| into a vector of pieces delimited by |delim|.
  static std::vector<QuicStringPiece> Split(QuicStringPiece data, char delim) {
    return QuicTextUtilsImpl::Split(data, delim);
  }
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_TEXT_UTILS_H_
