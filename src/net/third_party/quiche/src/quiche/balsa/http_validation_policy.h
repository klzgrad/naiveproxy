// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_HTTP_VALIDATION_POLICY_H_
#define QUICHE_BALSA_HTTP_VALIDATION_POLICY_H_

#include <cstdint>

#include "absl/strings/str_format.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// An HttpValidationPolicy captures policy choices affecting parsing of HTTP
// requests.  It offers individual Boolean members to be consulted during the
// parsing of an HTTP request.  For historical reasons, every member is set up
// such that `true` means more strict validation.
//
// NOTE: When modifying this struct's members, please update `AbslStringify()`
// below and `ArbitraryHttpValidationPolicy()` in balsa_fuzz_util.h.
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

  // TODO(b/314138604): remove this field once upstream Envoy stops using it
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

  // If true, then requests with a target URI that is invalid will be rejected.
  bool disallow_invalid_target_uris = false;

  // If SANITIZE, a request-line containing tab or carriage return will have
  // those characters replaced with space. If REJECT, a request-line containing
  // tab or carriage return will be rejected.
  enum class FirstLineValidationOption : uint8_t {
    NONE,
    SANITIZE,
    REJECT,
    kMinValue = NONE,
    kMaxValue = REJECT,
  };
  FirstLineValidationOption sanitize_cr_tab_in_first_line =
      FirstLineValidationOption::NONE;

  // If true, rejects messages with `obs-text` in header field names. RFC 9110
  // allows obs-text in header field values, but not names.
  bool disallow_obs_text_in_field_names = false;

  // If true, the parser rejects messages where there is a lone LF not preceded
  // by CR.
  bool disallow_lone_lf_in_chunk_extension = true;

  // If true, the parser rejects chunked messages that don't end with
  // CR_LF_CR_LF.
  bool require_chunked_body_end_with_crlf_crlf = false;

  // If SANITIZE, the parser will replace multiple consecutive spaces with
  // a single space in the HTTP/1 first line. If REJECT, a first line
  // containing multiple consecutive spaces will be rejected.
  FirstLineValidationOption sanitize_firstline_spaces =
      FirstLineValidationOption::NONE;

  // If true, the parser will replace obs-fold in header field values with one
  // or more space characters.
  bool sanitize_obs_fold_in_header_values = false;

  // If true, rejects messages with stray bytes after a HTTP chunk (before the
  // \r\n that separates it from the next chunk length).
  bool disallow_stray_data_after_chunk = false;

  // If true, disallow HTTP request methods that do not conform to RFC
  // 9110, Section 5.6.2.
  // https://datatracker.ietf.org/doc/html/rfc9110#section-5.6.2
  bool disallow_invalid_request_methods = false;

  // Chunk extensions are optional but, if they are present, they MUST be
  // preceded by a semicolon and follow the grammar:
  // chunk-ext = *( BWS ";" BWS chunk-ext-name [ BWS "=" BWS chunk-ext-val ] )
  // where BWS is SP or HTAB. See
  // https://datatracker.ietf.org/doc/html/rfc9112#name-chunk-extensions for
  // more information
  bool require_semicolon_delimited_chunk_extension = false;

  // Status codes outside the range [100, 599] are invalid, per RFC 9110,
  // Section 15 https://www.rfc-editor.org/rfc/rfc9110#section-15. Additionally,
  // status codes must begin with a digit within the range [1 - 5] and not
  // contain any non-digit characters.
  bool disallow_invalid_response_codes = false;
};

static constexpr HttpValidationPolicy kMostStrictHttpValidationPolicy = {
    .disallow_header_continuation_lines = true,
    .require_header_colon = true,
    .disallow_multiple_content_length = true,
    .disallow_transfer_encoding_with_content_length = true,
    .validate_transfer_encoding = true,
    .require_content_length_if_body_required = true,
    .disallow_double_quote_in_header_name = true,
    .disallow_invalid_header_characters_in_response = true,
    .disallow_lone_cr_in_request_headers = true,
    .disallow_lone_cr_in_chunk_extension = true,
    .disallow_invalid_target_uris = true,
    .sanitize_cr_tab_in_first_line =
        quiche::HttpValidationPolicy::FirstLineValidationOption::SANITIZE,
    .disallow_obs_text_in_field_names = true,
    .disallow_lone_lf_in_chunk_extension = true,
    .require_chunked_body_end_with_crlf_crlf = true,
    .sanitize_firstline_spaces =
        quiche::HttpValidationPolicy::FirstLineValidationOption::SANITIZE,
    .sanitize_obs_fold_in_header_values = true,
    .disallow_stray_data_after_chunk = true,
    .disallow_invalid_request_methods = true,
    .require_semicolon_delimited_chunk_extension = true,
    .disallow_invalid_response_codes = true,
};

}  // namespace quiche

#endif  // QUICHE_BALSA_HTTP_VALIDATION_POLICY_H_
