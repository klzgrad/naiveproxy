// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_METADATA_DECODER_H_
#define QUICHE_QUIC_CORE_HTTP_METADATA_DECODER_H_

#include <sys/types.h>

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/qpack/qpack_decoded_headers_accumulator.h"
#include "quiche/quic/core/qpack/qpack_decoder.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// Class for decoding the payload of HTTP/3 METADATA frames.
class QUICHE_EXPORT MetadataDecoder {
 public:
  MetadataDecoder(QuicStreamId id, size_t max_header_list_size,
                  size_t frame_header_len, size_t payload_length);

  // Incrementally decodes the next bytes of METADATA frame payload
  // and returns true if there were no errors.
  bool Decode(absl::string_view payload);

  // Finishes the decoding. Must be called after the full frame payload
  // has been decoded. Returns true if there were no errors.
  bool EndHeaderBlock();

  const std::string& error_message() { return decoder_.error_message(); }
  size_t frame_len() { return frame_len_; }
  const QuicHeaderList& headers() { return decoder_.headers(); }

 private:
  class MetadataHeadersDecoder
      : public QpackDecodedHeadersAccumulator::Visitor {
   public:
    // QpackDecodedHeadersAccumulator::Visitor
    void OnHeadersDecoded(QuicHeaderList headers,
                          bool header_list_size_limit_exceeded) override;
    void OnHeaderDecodingError(QuicErrorCode error_code,
                               absl::string_view error_message) override;

    QuicErrorCode error_code() { return error_code_; }
    const std::string& error_message() { return error_message_; }
    QuicHeaderList& headers() { return headers_; }
    bool header_list_size_limit_exceeded() {
      return header_list_size_limit_exceeded_;
    }

   private:
    QuicErrorCode error_code_ = QUIC_NO_ERROR;
    QuicHeaderList headers_;
    std::string error_message_;
    bool header_list_size_limit_exceeded_ = false;
  };

  NoopEncoderStreamErrorDelegate delegate_;
  QpackDecoder qpack_decoder_;
  MetadataHeadersDecoder decoder_;
  QpackDecodedHeadersAccumulator accumulator_;
  const size_t frame_len_;
  size_t bytes_remaining_ = 0;  // Debug only.
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_METADATA_DECODER_H_
