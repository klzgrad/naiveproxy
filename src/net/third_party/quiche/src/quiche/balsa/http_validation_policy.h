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
// parsing of an HTTP request.  For historical reasons, every member is set up
// such that `true` means more strict validation.
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

  // If true, signal an INVALID_HEADER_NAME_CHARACTER or
  // INVALID_TRAILER_NAME_CHARACTER error if the header or trailer name contains
  // the character '"'.
  bool disallow_double_quote_in_header_name = false;

  // If true, then signal an INVALID_HEADER_CHARACTER warning or error, or
  // neither, depending on InvalidCharsLevel, if a response header contains an
  // invalid character. Invalid characters are always disallowed according to
  // InvalidCharsLevel in request headers.
  bool disallow_invalid_header_characters_in_response = false;

  // If true, then signal an INVALID_HEADER_CHARACTER warning or error, or
  // neither, depending on InvalidCharsLevel, if a request header value contains
  // a carriage return that is not succeeded by a line feed.
  bool disallow_lone_cr_in_request_headers = false;

  // The RFC is quite specific about chunk extensions formatting, but we only
  // verify that there are no CR without a subsequent LF.
  bool disallow_lone_cr_in_chunk_extension = false;
};

}  // namespace quiche

#endif  // QUICHE_BALSA_HTTP_VALIDATION_POLICY_H_
