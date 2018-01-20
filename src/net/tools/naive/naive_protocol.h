// Copyright 2020 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_TOOLS_NAIVE_NAIVE_PROTOCOL_H_
#define NET_TOOLS_NAIVE_NAIVE_PROTOCOL_H_

#include <optional>
#include <string>

#include "base/strings/string_piece_forward.h"

namespace net {
enum class ClientProtocol {
  kSocks5,
  kHttp,
  kRedir,
};

const char* ToString(ClientProtocol value);

// Adds padding for traffic from this direction.
// Removes padding for traffic from the opposite direction.
enum Direction {
  kClient = 0,
  kServer = 1,
  kNumDirections = 2,
  kNone = 2,
};

enum class PaddingType {
  // Wire format: "0".
  kNone = 0,

  // Pads the first 8 reads and writes with padding bytes of random size
  // uniformly distributed in [0, 255].
  // struct PaddedFrame {
  //   uint8_t original_data_size_high;  // original_data_size / 256
  //   uint8_t original_data_size_low;  // original_data_size % 256
  //   uint8_t padding_size;
  //   uint8_t original_data[original_data_size];
  //   uint8_t zeros[padding_size];
  // };
  // Wire format: "1".
  kVariant1 = 1,
};

// Returns empty if `str` is invalid.
std::optional<PaddingType> ParsePaddingType(base::StringPiece str);

const char* ToString(PaddingType value);

const char* ToReadableString(PaddingType value);

constexpr const char* kPaddingHeader = "padding";

// Contains a comma separated list of requested padding types.
// Preferred types come first.
constexpr const char* kPaddingTypeRequestHeader = "padding-type-request";

// Contains a single number representing the negotiated padding type.
// Must be one of PaddingType.
constexpr const char* kPaddingTypeReplyHeader = "padding-type-reply";

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_PROTOCOL_H_
