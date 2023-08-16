// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_HTTP_VALIDATION_POLICY_H_
#define QUICHE_BALSA_HTTP_VALIDATION_POLICY_H_

#include <ostream>

#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// An HttpValidationPolicy captures policy choices affecting parsing of HTTP
// requests.  It offers individual Boolean members to be consulted during the
// parsing of an HTTP request.
struct QUICHE_EXPORT HttpValidationPolicy {
  // https://tools.ietf.org/html/rfc7230#section-3.2.4 deprecates "folding"
  // of long header lines onto continuation lines.
  bool disallow_header_continuation_lines = false;

  // A valid header line requires a header name and a colon.
  bool require_header_colon = false;

  // https://tools.ietf.org/html/rfc7230#section-3.3.2 disallows multiple
  // Content-Length header fields with the same value.
  bool disallow_multiple_content_length = false;

  // https://tools.ietf.org/html/rfc7230#section-3.3.2 disallows
  // Transfer-Encoding and Content-Length header fields together.
  bool disallow_transfer_encoding_with_content_length = false;

  // If true, signal an error if Transfer-Encoding has a value other than
  // "chunked" or "identity", or if there are multiple Transfer-Encoding field
  // lines. If false, ignore inconsistencies with Transfer-Encoding field lines,
  // also force `disallow_transfer_encoding_with_content_length` to false, but
  // still make an effort to determine whether chunked transfer encoding is
  // indicated.
  bool validate_transfer_encoding = true;

  // If true, signal a REQUIRED_BODY_BUT_NO_CONTENT_LENGTH error if a request
  // with a method POST or PUT, which requires a body, has neither a
  // "Content-Length" nor a "Transfer-Encoding: chunked" header.
  bool require_content_length_if_body_required = true;
};

}  // namespace quiche

#endif  // QUICHE_BALSA_HTTP_VALIDATION_POLICY_H_
