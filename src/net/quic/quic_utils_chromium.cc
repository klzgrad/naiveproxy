// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_utils_chromium.h"

#include "base/containers/adapters.h"
#include "base/strings/string_split.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace net {

quic::QuicTagVector ParseQuicConnectionOptions(
    const std::string& connection_options) {
  quic::QuicTagVector options;
  // Tokens are expected to be no more than 4 characters long, but
  // handle overflow gracefully.
  for (const quiche::QuicheStringPiece& token :
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

quic::ParsedQuicVersionVector ParseQuicVersions(
    const std::string& quic_versions) {
  quic::ParsedQuicVersionVector all_supported_versions =
      quic::AllSupportedVersions();
  quic::ParsedQuicVersionVector parsed_versions;
  for (const base::StringPiece& version_string : base::SplitStringPiece(
           quic_versions, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    auto it = all_supported_versions.begin();
    while (it != all_supported_versions.end()) {
      if ((it->handshake_protocol == quic::PROTOCOL_QUIC_CRYPTO &&
           quic::QuicVersionToString(it->transport_version) ==
               version_string) ||
          quic::AlpnForVersion(*it) == version_string) {
        parsed_versions.push_back(*it);
        // Remove the supported version to deduplicate versions extracted from
        // |quic_versions|.
        all_supported_versions.erase(it);
        break;
      }
      it++;
    }
  }
  return parsed_versions;
}

}  // namespace net
