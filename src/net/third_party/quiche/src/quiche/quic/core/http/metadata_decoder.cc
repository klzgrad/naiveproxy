// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/metadata_decoder.h"

#include <cstddef>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

MetadataDecoder::MetadataDecoder(QuicStreamId id, size_t max_header_list_size,
                                 size_t frame_header_len, size_t payload_length)
    : qpack_decoder_(/*maximum_dynamic_table_capacity=*/0,
                     /*maximum_blocked_streams=*/0, &delegate_),
      accumulator_(id, &qpack_decoder_, &decoder_, max_header_list_size),
      frame_len_(frame_header_len + payload_length),
      bytes_remaining_(payload_length) {}

bool MetadataDecoder::Decode(absl::string_view payload) {
  accumulator_.Decode(payload);
  bytes_remaining_ -= payload.length();
  return decoder_.error_code() == QUIC_NO_ERROR;
}

bool MetadataDecoder::EndHeaderBlock() {
  QUIC_BUG_IF(METADATA bytes remaining, bytes_remaining_ != 0)
      << "More metadata remaining: " << bytes_remaining_;

  accumulator_.EndHeaderBlock();
  return !decoder_.header_list_size_limit_exceeded();
}

void MetadataDecoder::MetadataHeadersDecoder::OnHeadersDecoded(
    QuicHeaderList headers, bool header_list_size_limit_exceeded) {
  header_list_size_limit_exceeded_ = header_list_size_limit_exceeded;
  headers_ = std::move(headers);
}

void MetadataDecoder::MetadataHeadersDecoder::OnHeaderDecodingError(
    QuicErrorCode error_code, absl::string_view error_message) {
  error_code_ = error_code;
  error_message_ = absl::StrCat("Error decoding metadata: ", error_message);
}

}  // namespace quic
