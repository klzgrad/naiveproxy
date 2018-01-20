// Copyright 2023 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/tools/naive/naive_protocol.h"

#include <optional>
#include <string>

#include "base/strings/string_piece.h"

namespace net {
const char* ToString(ClientProtocol value) {
  switch (value) {
    case ClientProtocol::kSocks5:
      return "socks";
    case ClientProtocol::kHttp:
      return "http";
    case ClientProtocol::kRedir:
      return "redir";
    default:
      return "";
  }
}

std::optional<PaddingType> ParsePaddingType(base::StringPiece str) {
  if (str == "0") {
    return PaddingType::kNone;
  } else if (str == "1") {
    return PaddingType::kVariant1;
  } else {
    return std::nullopt;
  }
}

const char* ToString(PaddingType value) {
  switch (value) {
    case PaddingType::kNone:
      return "0";
    case PaddingType::kVariant1:
      return "1";
    default:
      return "";
  }
}

const char* ToReadableString(PaddingType value) {
  switch (value) {
    case PaddingType::kNone:
      return "None";
    case PaddingType::kVariant1:
      return "Variant1";
    default:
      return "";
  }
}

}  // namespace net
