// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/base64.h"

#include "base/strings/string_util.h"
#include "net/base/features.h"
#include "third_party/simdutf/simdutf.h"

namespace net {

bool SimdutfBase64Decode(std::string_view input,
                         std::string* output,
                         base::Base64DecodePolicy policy) {
  std::string decode_buf;
  if (policy == base::Base64DecodePolicy::kStrict) {
    if (input.size() % 4 != 0) {
      // The input is not properly padded.
      return false;
    }
    std::string_view bad_chars(base::kInfraAsciiWhitespace);
    for (char c : bad_chars) {
      if (input.find(c) != std::string_view::npos) {
        return false;
      }
    }
  }
  decode_buf.resize(
      simdutf::maximal_binary_length_from_base64(input.data(), input.size()));
  simdutf::result r =
      simdutf::base64_to_binary(input.data(), input.size(), decode_buf.data());
  if (r.error != simdutf::error_code::SUCCESS) {
    return false;
  }
  // If this failed it would indicate we wrote OOB. It's possible for this to
  // be elided by the compiler, since writing OOB is UB.
  CHECK_LE(r.count, decode_buf.size());

  // Shrinks the buffer and makes it NUL-terminated.
  decode_buf.resize(r.count);

  *output = std::move(decode_buf);
  return true;
}

bool IsSimdutfBase64SupportEnabled() {
  static const bool simdutf_base64_support_enabled =
      base::FeatureList::IsEnabled(features::kSimdutfBase64Support);
  return simdutf_base64_support_enabled;
}

}  // namespace net
