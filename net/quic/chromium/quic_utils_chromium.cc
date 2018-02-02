// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/quic_utils_chromium.h"

#include "base/containers/adapters.h"
#include "base/strings/string_split.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

QuicTagVector ParseQuicConnectionOptions(
    const std::string& connection_options) {
  QuicTagVector options;
  // Tokens are expected to be no more than 4 characters long, but
  // handle overflow gracefully.
  for (const QuicStringPiece& token :
       base::SplitStringPiece(connection_options, ",", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_ALL)) {
    uint32_t option = 0;
    for (char token_char : base::Reversed(token)) {
      option <<= 8;
      option |= static_cast<unsigned char>(token_char);
    }
    options.push_back(option);
  }
  return options;
}

}  // namespace net
