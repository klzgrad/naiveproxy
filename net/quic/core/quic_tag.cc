// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_tag.h"

#include "base/macros.h"
#include "base/stl_util.h"
#include "net/quic/platform/api/quic_text_utils.h"

namespace net {

bool FindMutualQuicTag(const QuicTagVector& our_tags,
                       const QuicTagVector& their_tags,
                       QuicTag* out_result,
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
  char chars[sizeof tag];
  bool ascii = true;
  const QuicTag orig_tag = tag;

  for (size_t i = 0; i < arraysize(chars); i++) {
    chars[i] = static_cast<char>(tag);
    if ((chars[i] == 0 || chars[i] == '\xff') && i == arraysize(chars) - 1) {
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

  return QuicTextUtils::Uint64ToString(orig_tag);
}

uint32_t MakeQuicTag(char a, char b, char c, char d) {
  return static_cast<uint32_t>(a) | static_cast<uint32_t>(b) << 8 |
         static_cast<uint32_t>(c) << 16 | static_cast<uint32_t>(d) << 24;
}

bool ContainsQuicTag(const QuicTagVector& tag_vector, QuicTag tag) {
  return base::ContainsValue(tag_vector, tag);
}

}  // namespace net
