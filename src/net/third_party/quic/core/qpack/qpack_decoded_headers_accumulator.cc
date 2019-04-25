// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_decoded_headers_accumulator.h"

#include "net/third_party/quic/core/qpack/qpack_decoder.h"

namespace quic {

QpackDecodedHeadersAccumulator::QpackDecodedHeadersAccumulator(
    QuicStreamId id,
    QpackDecoder* qpack_decoder)
    : decoder_(qpack_decoder->DecodeHeaderBlock(id, this)),
      uncompressed_header_bytes_(0),
      compressed_header_bytes_(0),
      error_detected_(false) {
  quic_header_list_.OnHeaderBlockStart();
}

void QpackDecodedHeadersAccumulator::OnHeaderDecoded(QuicStringPiece name,
                                                     QuicStringPiece value) {
  DCHECK(!error_detected_);

  uncompressed_header_bytes_ += name.size() + value.size();
  quic_header_list_.OnHeader(name, value);
}

void QpackDecodedHeadersAccumulator::OnDecodingCompleted() {}

void QpackDecodedHeadersAccumulator::OnDecodingErrorDetected(
    QuicStringPiece error_message) {
  DCHECK(!error_detected_);

  error_detected_ = true;
  // Copy error message to ensure it remains valid for the lifetime of |this|.
  error_message_.assign(error_message.data(), error_message.size());
}

bool QpackDecodedHeadersAccumulator::Decode(QuicStringPiece data) {
  DCHECK(!error_detected_);

  compressed_header_bytes_ += data.size();
  decoder_->Decode(data);

  return !error_detected_;
}

bool QpackDecodedHeadersAccumulator::EndHeaderBlock() {
  DCHECK(!error_detected_);

  decoder_->EndHeaderBlock();

  quic_header_list_.OnHeaderBlockEnd(uncompressed_header_bytes_,
                                     compressed_header_bytes_);

  return !error_detected_;
}

const QuicHeaderList& QpackDecodedHeadersAccumulator::quic_header_list() const {
  DCHECK(!error_detected_);
  return quic_header_list_;
}

QuicStringPiece QpackDecodedHeadersAccumulator::error_message() const {
  DCHECK(error_detected_);
  return error_message_;
}

}  // namespace quic
