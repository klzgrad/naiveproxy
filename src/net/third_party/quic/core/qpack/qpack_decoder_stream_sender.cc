// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_decoder_stream_sender.h"

#include <cstddef>
#include <limits>

#include "net/third_party/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

QpackDecoderStreamSender::QpackDecoderStreamSender(Delegate* delegate)
    : delegate_(delegate) {
  DCHECK(delegate_);
}

void QpackDecoderStreamSender::SendTableStateSynchronize(
    uint64_t insert_count) {
  instruction_encoder_.set_varint(insert_count);

  instruction_encoder_.Encode(TableStateSynchronizeInstruction());

  QuicString output;

  instruction_encoder_.Next(std::numeric_limits<size_t>::max(), &output);
  DCHECK(!instruction_encoder_.HasNext());

  delegate_->Write(output);
}

void QpackDecoderStreamSender::SendHeaderAcknowledgement(
    QuicStreamId stream_id) {
  instruction_encoder_.set_varint(stream_id);

  instruction_encoder_.Encode(HeaderAcknowledgementInstruction());

  QuicString output;

  instruction_encoder_.Next(std::numeric_limits<size_t>::max(), &output);
  DCHECK(!instruction_encoder_.HasNext());

  delegate_->Write(output);
}

void QpackDecoderStreamSender::SendStreamCancellation(QuicStreamId stream_id) {
  instruction_encoder_.set_varint(stream_id);

  instruction_encoder_.Encode(StreamCancellationInstruction());

  QuicString output;

  instruction_encoder_.Next(std::numeric_limits<size_t>::max(), &output);
  DCHECK(!instruction_encoder_.HasNext());

  delegate_->Write(output);
}

}  // namespace quic
