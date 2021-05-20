// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/qpack/qpack_decoded_headers_accumulator.h"

#include "absl/strings/string_view.h"
#include "quic/core/qpack/qpack_decoder.h"
#include "quic/core/qpack/qpack_header_table.h"

namespace quic {

QpackDecodedHeadersAccumulator::QpackDecodedHeadersAccumulator(
    QuicStreamId id,
    QpackDecoder* qpack_decoder,
    Visitor* visitor,
    size_t max_header_list_size)
    : decoder_(qpack_decoder->CreateProgressiveDecoder(id, this)),
      visitor_(visitor),
      max_header_list_size_(max_header_list_size),
      uncompressed_header_bytes_including_overhead_(0),
      uncompressed_header_bytes_without_overhead_(0),
      compressed_header_bytes_(0),
      header_list_size_limit_exceeded_(false),
      headers_decoded_(false),
      error_detected_(false) {
  quic_header_list_.OnHeaderBlockStart();
}

void QpackDecodedHeadersAccumulator::OnHeaderDecoded(absl::string_view name,
                                                     absl::string_view value) {
  QUICHE_DCHECK(!error_detected_);

  uncompressed_header_bytes_without_overhead_ += name.size() + value.size();

  if (header_list_size_limit_exceeded_) {
    return;
  }

  uncompressed_header_bytes_including_overhead_ +=
      name.size() + value.size() + QpackEntry::kSizeOverhead;

  if (uncompressed_header_bytes_including_overhead_ > max_header_list_size_) {
    header_list_size_limit_exceeded_ = true;
    quic_header_list_.Clear();
  } else {
    quic_header_list_.OnHeader(name, value);
  }
}

void QpackDecodedHeadersAccumulator::OnDecodingCompleted() {
  QUICHE_DCHECK(!headers_decoded_);
  QUICHE_DCHECK(!error_detected_);

  headers_decoded_ = true;

  quic_header_list_.OnHeaderBlockEnd(
      uncompressed_header_bytes_without_overhead_, compressed_header_bytes_);

  // Might destroy |this|.
  visitor_->OnHeadersDecoded(std::move(quic_header_list_),
                             header_list_size_limit_exceeded_);
}

void QpackDecodedHeadersAccumulator::OnDecodingErrorDetected(
    absl::string_view error_message) {
  QUICHE_DCHECK(!error_detected_);
  QUICHE_DCHECK(!headers_decoded_);

  error_detected_ = true;
  // Might destroy |this|.
  visitor_->OnHeaderDecodingError(error_message);
}

void QpackDecodedHeadersAccumulator::Decode(absl::string_view data) {
  QUICHE_DCHECK(!error_detected_);

  compressed_header_bytes_ += data.size();
  // Might destroy |this|.
  decoder_->Decode(data);
}

void QpackDecodedHeadersAccumulator::EndHeaderBlock() {
  QUICHE_DCHECK(!error_detected_);
  QUICHE_DCHECK(!headers_decoded_);

  // Might destroy |this|.
  decoder_->EndHeaderBlock();
}

}  // namespace quic
