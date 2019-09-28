// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_stream_sender.h"

#include <cstddef>
#include <limits>
#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

QpackDecoderStreamSender::QpackDecoderStreamSender(
    QpackStreamSenderDelegate* delegate)
    : delegate_(delegate) {
  DCHECK(delegate_);
}

void QpackDecoderStreamSender::SendInsertCountIncrement(uint64_t increment) {
  values_.varint = increment;

  std::string output;
  instruction_encoder_.Encode(InsertCountIncrementInstruction(), values_,
                              &output);
  delegate_->WriteStreamData(output);
}

void QpackDecoderStreamSender::SendHeaderAcknowledgement(
    QuicStreamId stream_id) {
  values_.varint = stream_id;

  std::string output;
  instruction_encoder_.Encode(HeaderAcknowledgementInstruction(), values_,
                              &output);
  delegate_->WriteStreamData(output);
}

void QpackDecoderStreamSender::SendStreamCancellation(QuicStreamId stream_id) {
  values_.varint = stream_id;

  std::string output;
  instruction_encoder_.Encode(StreamCancellationInstruction(), values_,
                              &output);
  delegate_->WriteStreamData(output);
}

}  // namespace quic
