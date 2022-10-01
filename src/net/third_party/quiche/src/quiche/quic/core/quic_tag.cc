// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_tag.h"

#include <algorithm>
#include <string>

#include "absl/base/macros.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {

bool FindMutualQuicTag(const QuicTagVector& our_tags,
                       const QuicTagVector& their_tags, QuicTag* out_result,
                       size_t* out_index) {
  const size_t num_our_tags = our_tags.size();
  const size_t num_their_tags = their_tags.size();
  for (size_t i = 0; i < num_our_tags; i++) {
    for (size_t j = 0; j < num_their_tags; j++) {
      if (our_tags[i] == their_tags[j]) {
        *out_result = our_tags[i];
        if (out_index != nullptr) {
          *out_index = j;
        }
        return true;
      }
    }
  }

  return false;
}

std::string QuicTagToString(QuicTag tag) {
  if (tag == 0) {
    return "0";
  }
  char chars[sizeof tag];
  bool ascii = true;
  const QuicTag orig_tag = tag;

  for (size_t i = 0; i < ABSL_ARRAYSIZE(chars); i++) {
    chars[i] = static_cast<char>(tag);
    if ((chars[i] == 0 || chars[i] == '\xff') &&
        i == ABSL_ARRAYSIZE(chars) - 1) {
      chars[i] = ' ';
    }
    if (!isprint(static_cast<unsigned char>(chars[i]))) {
      ascii = false;
      break;
    }
    tag >>= 8;
  }

  if (ascii) {
    return std::string(chars, sizeof(chars));
  }

  return absl::BytesToHexString(absl::string_view(
      reinterpret_cast<const char*>(&orig_tag), sizeof(orig_tag)));
}

uint32_t MakeQuicTag(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return static_cast<uint32_t>(a) | static_cast<uint32_t>(b) << 8 |
         static_cast<uint32_t>(c) << 16 | static_cast<uint32_t>(d) << 24;
}

bool ContainsQuicTag(const QuicTagVector& tag_vector, QuicTag tag) {
  return std::find(tag_vector.begin(), tag_vector.end(), tag) !=
         tag_vector.end();
}

QuicTag ParseQuicTag(absl::string_view tag_string) {
  quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&tag_string);
  std::string tag_bytes;
  if (tag_string.length() == 8) {
    tag_bytes = absl::HexStringToBytes(tag_string);
    tag_string = tag_bytes;
  }
  QuicTag tag = 0;
  // Iterate over every character from right to left.
  for (auto it = tag_string.rbegin(); it != tag_string.rend(); ++it) {
    // The cast here is required on platforms where char is signed.
    unsigned char token_char = static_cast<unsigned char>(*it);
    tag <<= 8;
    tag |= token_char;
  }
  return tag;
}

QuicTagVector ParseQuicTagVector(absl::string_view tags_string) {
  QuicTagVector tag_vector;
  quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&tags_string);
  if (!tags_string.empty()) {
    std::vector<absl::string_view> tag_strings =
        absl::StrSplit(tags_string, ',');
    for (absl::string_view tag_string : tag_strings) {
      tag_vector.push_back(ParseQuicTag(tag_string));
    }
  }
  return tag_vector;
}

}  // namespace quic
