// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/quiche_text_utils.h"

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace quiche {

// static
void QuicheTextUtils::Base64Encode(const uint8_t* data, size_t data_len,
                                   std::string* output) {
  absl::Base64Escape(std::string(reinterpret_cast<const char*>(data), data_len),
                     output);
  // Remove padding.
  size_t len = output->size();
  if (len >= 2) {
    if ((*output)[len - 1] == '=') {
      len--;
      if ((*output)[len - 1] == '=') {
        len--;
      }
      output->resize(len);
    }
  }
}

// static
absl::optional<std::string> QuicheTextUtils::Base64Decode(
    absl::string_view input) {
  std::string output;
  if (!absl::Base64Unescape(input, &output)) {
    return absl::nullopt;
  }
  return output;
}

// static
std::string QuicheTextUtils::HexDump(absl::string_view binary_data) {
  const int kBytesPerLine = 16;  // Maximum bytes dumped per line.
  int offset = 0;
  const char* p = binary_data.data();
  int bytes_remaining = binary_data.size();
  std::string output;
  while (bytes_remaining > 0) {
    const int line_bytes = std::min(bytes_remaining, kBytesPerLine);
    absl::StrAppendFormat(&output, "0x%04x:  ", offset);
    for (int i = 0; i < kBytesPerLine; ++i) {
      if (i < line_bytes) {
        absl::StrAppendFormat(&output, "%02x",
                              static_cast<unsigned char>(p[i]));
      } else {
        absl::StrAppend(&output, "  ");
      }
      if (i % 2) {
        absl::StrAppend(&output, " ");
      }
    }
    absl::StrAppend(&output, " ");
    for (int i = 0; i < line_bytes; ++i) {
      // Replace non-printable characters and 0x20 (space) with '.'
      output += absl::ascii_isgraph(p[i]) ? p[i] : '.';
    }

    bytes_remaining -= line_bytes;
    offset += line_bytes;
    p += line_bytes;
    absl::StrAppend(&output, "\n");
  }
  return output;
}

}  // namespace quiche
