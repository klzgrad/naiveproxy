// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoded_headers_accumulator.h"

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace {

size_t kHeaderFieldSizeOverhead = 32;

}

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

void QpackDecodedHeadersAccumulator::OnHeaderDecoded(
    quiche::QuicheStringPiece name,
    quiche::QuicheStringPiece value) {
  DCHECK(!error_detected_);

  uncompressed_header_bytes_without_overhead_ += name.size() + value.size();

  if (header_list_size_limit_exceeded_) {
    return;
  }

  uncompressed_header_bytes_including_overhead_ +=
      name.size() + value.size() + kHeaderFieldSizeOverhead;

  if (uncompressed_header_bytes_including_overhead_ > max_header_list_size_) {
    header_list_size_limit_exceeded_ = true;
    quic_header_list_.Clear();
  } else {
    quic_header_list_.OnHeader(name, value);
  }
}

void QpackDecodedHeadersAccumulator::OnDecodingCompleted() {
  DCHECK(!headers_decoded_);
  DCHECK(!error_detected_);

  headers_decoded_ = true;

  quic_header_list_.OnHeaderBlockEnd(
      uncompressed_header_bytes_without_overhead_, compressed_header_bytes_);

  // Might destroy |this|.
  visitor_->OnHeadersDecoded(std::move(quic_header_list_),
                             header_list_size_limit_exceeded_);
}

void QpackDecodedHeadersAccumulator::OnDecodingErrorDetected(
    quiche::QuicheStringPiece error_message) {
  DCHECK(!error_detected_);
  DCHECK(!headers_decoded_);

  error_detected_ = true;
  // Might destroy |this|.
  visitor_->OnHeaderDecodingError(error_message);
}

void QpackDecodedHeadersAccumulator::Decode(quiche::QuicheStringPiece data) {
  DCHECK(!error_detected_);

  compressed_header_bytes_ += data.size();
  // Might destroy |this|.
  decoder_->Decode(data);
}

void QpackDecodedHeadersAccumulator::EndHeaderBlock() {
  DCHECK(!error_detected_);
  DCHECK(!headers_decoded_);

  // Might destroy |this|.
  decoder_->EndHeaderBlock();
}

}  // namespace quic
