// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_HTTP_VALIDATION_POLICY_H_
#define QUICHE_BALSA_HTTP_VALIDATION_POLICY_H_

#include <ostream>

#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// An HttpValidationPolicy captures policy choices affecting parsing of HTTP
// requests.  It offers individual Boolean member functions to be consulted
// during the parsing of an HTTP request.
class QUICHE_EXPORT HttpValidationPolicy {
 public:
  HttpValidationPolicy(bool enforce_all);

  static HttpValidationPolicy CreateDefault();

  // https://tools.ietf.org/html/rfc7230#section-3.2.4 deprecates "folding"
  // of long header lines onto continuation lines.
  bool disallow_header_continuation_lines() const { return enforce_all_; }

  // A valid header line requires a header name and a colon.
  bool require_header_colon() const { return enforce_all_; }

  // https://tools.ietf.org/html/rfc7230#section-3.3.2 disallows multiple
  // Content-Length header fields with the same value.
  bool disallow_multiple_content_length() const { return enforce_all_; }

  // https://tools.ietf.org/html/rfc7230#section-3.3.2 disallows
  // Transfer-Encoding and Content-Length header fields together.
  bool disallow_transfer_encoding_with_content_length() const {
    return enforce_all_;
  }

  bool operator==(const HttpValidationPolicy& other) const;

  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const HttpValidationPolicy& policy) {
    os << "HttpValidationPolicy(enforce_all_=" << policy.enforce_all_ << ")";
    return os;
  }

 private:
  // Enforce "everything": set for strictest possible parsing.
  bool enforce_all_;
};

}  // namespace quiche

#endif  // QUICHE_BALSA_HTTP_VALIDATION_POLICY_H_
