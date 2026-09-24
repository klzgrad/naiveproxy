// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_HTTP_PROTOCOL_DEFECTS_H_
#define QUICHE_BALSA_HTTP_PROTOCOL_DEFECTS_H_

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

class QUICHE_EXPORT HttpProtocolDefects {
 public:
  bool invalid_method_in_request_first_line = false;
  bool multiple_content_length_keys = false;
  bool missing_semicolon_in_chunk_extension = false;
  bool token_before_semicolon_in_chunk_extension = false;
  bool invalid_response_code = false;
  bool multiple_spaces_in_firstline = false;
  bool obs_fold_in_header_values = false;
  bool obs_fold_in_trailer_values = false;
  bool chunked_body_does_not_end_with_crlf_crlf = false;
  bool stray_data_after_chunk = false;
  bool lone_cr_in_request_headers = false;
  bool header_name_contains_double_quote = false;
  bool header_missing_colon = false;
  bool tab_or_cr_found_in_firstline = false;
  bool obs_text_found_in_header_name = false;
  bool transfer_encoding_and_content_length = false;
  bool unknown_transfer_encoding = false;
  bool multiple_transfer_encoding_keys = false;

  void Merge(const HttpProtocolDefects& other) {
    invalid_method_in_request_first_line |=
        other.invalid_method_in_request_first_line;
    multiple_content_length_keys |= other.multiple_content_length_keys;
    missing_semicolon_in_chunk_extension |=
        other.missing_semicolon_in_chunk_extension;
    token_before_semicolon_in_chunk_extension |=
        other.token_before_semicolon_in_chunk_extension;
    invalid_response_code |= other.invalid_response_code;
    multiple_spaces_in_firstline |= other.multiple_spaces_in_firstline;
    obs_fold_in_header_values |= other.obs_fold_in_header_values;
    obs_fold_in_trailer_values |= other.obs_fold_in_trailer_values;
    chunked_body_does_not_end_with_crlf_crlf |=
        other.chunked_body_does_not_end_with_crlf_crlf;
    stray_data_after_chunk |= other.stray_data_after_chunk;
    lone_cr_in_request_headers |= other.lone_cr_in_request_headers;
    header_name_contains_double_quote |=
        other.header_name_contains_double_quote;
    header_missing_colon |= other.header_missing_colon;
    tab_or_cr_found_in_firstline |= other.tab_or_cr_found_in_firstline;
    obs_text_found_in_header_name |= other.obs_text_found_in_header_name;
    transfer_encoding_and_content_length |=
        other.transfer_encoding_and_content_length;
    unknown_transfer_encoding |= other.unknown_transfer_encoding;
    multiple_transfer_encoding_keys |= other.multiple_transfer_encoding_keys;
  }

  void ForEachDefect(
      UnretainedCallback<void(absl::string_view)> callback) const {
    if (invalid_method_in_request_first_line) {
      callback("invalid_method_in_request_first_line");
    }
    if (multiple_content_length_keys) {
      callback("multiple_content_length_keys");
    }
    if (missing_semicolon_in_chunk_extension) {
      callback("missing_semicolon_in_chunk_extension");
    }
    if (token_before_semicolon_in_chunk_extension) {
      callback("token_before_semicolon_in_chunk_extension");
    }
    if (invalid_response_code) {
      callback("invalid_response_code");
    }
    if (multiple_spaces_in_firstline) {
      callback("multiple_spaces_in_firstline");
    }
    if (obs_fold_in_header_values) {
      callback("obs_fold_in_header_values");
    }
    if (obs_fold_in_trailer_values) {
      callback("obs_fold_in_trailer_values");
    }
    if (chunked_body_does_not_end_with_crlf_crlf) {
      callback("chunked_body_does_not_end_with_crlf_crlf");
    }
    if (stray_data_after_chunk) {
      callback("stray_data_after_chunk");
    }
    if (lone_cr_in_request_headers) {
      callback("lone_cr_in_request_headers");
    }
    if (header_name_contains_double_quote) {
      callback("header_name_contains_double_quote");
    }
    if (header_missing_colon) {
      callback("header_missing_colon");
    }
    if (tab_or_cr_found_in_firstline) {
      callback("tab_or_cr_found_in_firstline");
    }
    if (obs_text_found_in_header_name) {
      callback("obs_text_found_in_header_name");
    }
    if (transfer_encoding_and_content_length) {
      callback("transfer_encoding_and_content_length");
    }
    if (unknown_transfer_encoding) {
      callback("unknown_transfer_encoding");
    }
    if (multiple_transfer_encoding_keys) {
      callback("multiple_transfer_encoding_keys");
    }
  }

  // T should ideally be strongly typed as
  // logs::gfe::GfeLog::HttpProtocolDefects but since Quiche can only depend on
  // targets that are compatible with non_prod, a template is used instead.
  template <typename T>
  void PopulateLogDefects(T& log_defects) const {
    if (invalid_method_in_request_first_line) {
      log_defects.set_invalid_method_in_request_first_line(true);
    }
    if (multiple_content_length_keys) {
      log_defects.set_multiple_content_length_keys_detected(true);
    }
    if (missing_semicolon_in_chunk_extension) {
      log_defects.set_missing_semicolon_in_chunk_extension(true);
    }
    if (token_before_semicolon_in_chunk_extension) {
      log_defects.set_token_before_semicolon_in_chunk_extension(true);
    }
    if (invalid_response_code) {
      log_defects.set_invalid_response_code(true);
    }
    if (multiple_spaces_in_firstline) {
      log_defects.set_multiple_spaces_found_in_first_line(true);
    }
    if (obs_fold_in_header_values) {
      log_defects.set_obs_fold_in_header_value(true);
    }
    if (obs_fold_in_trailer_values) {
      log_defects.set_obs_fold_in_trailer_value(true);
    }
    if (chunked_body_does_not_end_with_crlf_crlf) {
      log_defects.set_chunked_request_ends_with_crlflf(true);
    }
    if (stray_data_after_chunk) {
      log_defects.set_stray_data_after_chunk(true);
    }
    if (lone_cr_in_request_headers) {
      log_defects.set_cr_found_in_header_value(true);
    }
    if (header_name_contains_double_quote) {
      log_defects.set_header_name_contains_double_quote(true);
    }
    if (header_missing_colon) {
      log_defects.set_header_missing_colon(true);
    }
    if (transfer_encoding_and_content_length) {
      log_defects.set_transfer_encoding_and_content_length(true);
    }
    if (unknown_transfer_encoding) {
      log_defects.set_unknown_transfer_encoding(true);
    }
    if (multiple_transfer_encoding_keys) {
      log_defects.set_multiple_transfer_encoding_keys(true);
    }
    if (tab_or_cr_found_in_firstline) {
      log_defects.set_tab_or_cr_found_in_firstline(true);
    }
    if (obs_text_found_in_header_name) {
      log_defects.set_obs_text_found_in_header_name(true);
    }
  }
};

}  // namespace quiche

#endif  // QUICHE_BALSA_HTTP_PROTOCOL_DEFECTS_H_
