// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_stream_sender.h"

#include <cstddef>
#include <limits>
#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instructions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QpackDecoderStreamSender::QpackDecoderStreamSender() : delegate_(nullptr) {}

void QpackDecoderStreamSender::SendInsertCountIncrement(uint64_t increment) {
  instruction_encoder_.Encode(
      QpackInstructionWithValues::InsertCountIncrement(increment), &buffer_);
}

void QpackDecoderStreamSender::SendHeaderAcknowledgement(
    QuicStreamId stream_id) {
  instruction_encoder_.Encode(
      QpackInstructionWithValues::HeaderAcknowledgement(stream_id), &buffer_);
}

void QpackDecoderStreamSender::SendStreamCancellation(QuicStreamId stream_id) {
  instruction_encoder_.Encode(
      QpackInstructionWithValues::StreamCancellation(stream_id), &buffer_);
}

void QpackDecoderStreamSender::Flush() {
  if (buffer_.empty()) {
    return;
  }

  delegate_->WriteStreamData(buffer_);
  buffer_.clear();
}

}  // namespace quic
