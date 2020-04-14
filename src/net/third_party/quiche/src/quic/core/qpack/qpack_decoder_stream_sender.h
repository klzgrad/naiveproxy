// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_SENDER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_SENDER_H_

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instruction_encoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_stream_sender_delegate.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// This class serializes instructions for transmission on the decoder stream.
// Serialized instructions are buffered until Flush() is called.
class QUIC_EXPORT_PRIVATE QpackDecoderStreamSender {
 public:
  QpackDecoderStreamSender();
  QpackDecoderStreamSender(const QpackDecoderStreamSender&) = delete;
  QpackDecoderStreamSender& operator=(const QpackDecoderStreamSender&) = delete;

  // Methods for serializing and buffering instructions, see
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5.3

  // 5.3.1 Insert Count Increment
  void SendInsertCountIncrement(uint64_t increment);
  // 5.3.2 Header Acknowledgement
  void SendHeaderAcknowledgement(QuicStreamId stream_id);
  // 5.3.3 Stream Cancellation
  void SendStreamCancellation(QuicStreamId stream_id);

  // Writes all buffered instructions on the decoder stream.
  void Flush();

  // delegate must be set if dynamic table capacity is not zero.
  void set_qpack_stream_sender_delegate(QpackStreamSenderDelegate* delegate) {
    delegate_ = delegate;
  }

 private:
  QpackStreamSenderDelegate* delegate_;
  QpackInstructionEncoder instruction_encoder_;
  std::string buffer_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_SENDER_H_
