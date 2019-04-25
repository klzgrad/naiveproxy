// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_SENDER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_SENDER_H_

#include <cstdint>

#include "net/third_party/quic/core/qpack/qpack_instruction_encoder.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

// This class serializes (encodes) instructions for transmission on the decoder
// stream.
class QUIC_EXPORT_PRIVATE QpackDecoderStreamSender {
 public:
  // An interface for handling encoded data.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Encoded |data| is ready to be written on the decoder stream.
    // WriteDecoderStreamData() is called exactly once for each instruction.
    // |data| contains the entire encoded instruction and it is guaranteed to be
    // not empty.
    virtual void WriteDecoderStreamData(QuicStringPiece data) = 0;
  };

  explicit QpackDecoderStreamSender(Delegate* delegate);
  QpackDecoderStreamSender() = delete;
  QpackDecoderStreamSender(const QpackDecoderStreamSender&) = delete;
  QpackDecoderStreamSender& operator=(const QpackDecoderStreamSender&) = delete;

  // Methods for sending instructions, see
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5.3

  // 5.3.1 Insert Count Increment
  void SendInsertCountIncrement(uint64_t increment);
  // 5.3.2 Header Acknowledgement
  void SendHeaderAcknowledgement(QuicStreamId stream_id);
  // 5.3.3 Stream Cancellation
  void SendStreamCancellation(QuicStreamId stream_id);

 private:
  Delegate* const delegate_;
  QpackInstructionEncoder instruction_encoder_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_SENDER_H_
