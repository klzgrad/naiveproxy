// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/encoding_detection.h"

#include "third_party/ced/src/compact_enc_det/compact_enc_det.h"

namespace base {

bool DetectEncoding(const std::string& text, std::string* encoding) {
  int consumed_bytes;
  bool is_reliable;
  Encoding enc = CompactEncDet::DetectEncoding(
      text.c_str(), text.length(), nullptr, nullptr, nullptr,
      UNKNOWN_ENCODING,
      UNKNOWN_LANGUAGE,
      CompactEncDet::QUERY_CORPUS,  // plain text
      false,  // Include 7-bit encodings
      &consumed_bytes,
      &is_reliable);

  if (enc == UNKNOWN_ENCODING)
    return false;

  *encoding = MimeEncodingName(enc);
  return true;
}
}  // namespace base
