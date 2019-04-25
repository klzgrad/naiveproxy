// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_encoder.h"

#include "base/logging.h"
#include "net/third_party/quic/core/qpack/qpack_progressive_encoder.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

QpackEncoder::QpackEncoder(
    DecoderStreamErrorDelegate* decoder_stream_error_delegate,
    QpackEncoderStreamSender::Delegate* encoder_stream_sender_delegate)
    : decoder_stream_error_delegate_(decoder_stream_error_delegate),
      decoder_stream_receiver_(this),
      encoder_stream_sender_(encoder_stream_sender_delegate) {
  DCHECK(decoder_stream_error_delegate_);
  DCHECK(encoder_stream_sender_delegate);
}

QpackEncoder::~QpackEncoder() {}

std::unique_ptr<spdy::HpackEncoder::ProgressiveEncoder>
QpackEncoder::EncodeHeaderList(QuicStreamId stream_id,
                               const spdy::SpdyHeaderBlock* header_list) {
  return QuicMakeUnique<QpackProgressiveEncoder>(
      stream_id, &header_table_, &encoder_stream_sender_, header_list);
}

void QpackEncoder::DecodeDecoderStreamData(QuicStringPiece data) {
  decoder_stream_receiver_.Decode(data);
}

void QpackEncoder::OnInsertCountIncrement(uint64_t increment) {
  // TODO(bnc): Implement dynamic table management for encoding.
}

void QpackEncoder::OnHeaderAcknowledgement(QuicStreamId stream_id) {
  // TODO(bnc): Implement dynamic table management for encoding.
}

void QpackEncoder::OnStreamCancellation(QuicStreamId stream_id) {
  // TODO(bnc): Implement dynamic table management for encoding.
}

void QpackEncoder::OnErrorDetected(QuicStringPiece error_message) {
  decoder_stream_error_delegate_->OnDecoderStreamError(error_message);
}

}  // namespace quic
