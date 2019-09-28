// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_H_

#include <cstdint>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_stream_receiver.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_stream_sender.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

namespace spdy {

class SpdyHeaderBlock;

}

namespace quic {

// QPACK encoder class.  Exactly one instance should exist per QUIC connection.
class QUIC_EXPORT_PRIVATE QpackEncoder
    : public QpackDecoderStreamReceiver::Delegate {
 public:
  // Interface for receiving notification that an error has occurred on the
  // decoder stream.  This MUST be treated as a connection error of type
  // HTTP_QPACK_DECODER_STREAM_ERROR.
  class QUIC_EXPORT_PRIVATE DecoderStreamErrorDelegate {
   public:
    virtual ~DecoderStreamErrorDelegate() {}

    virtual void OnDecoderStreamError(QuicStringPiece error_message) = 0;
  };

  QpackEncoder(DecoderStreamErrorDelegate* decoder_stream_error_delegate,
               QpackStreamSenderDelegate* encoder_stream_sender_delegate);
  ~QpackEncoder() override;

  // Encode a header list.
  std::string EncodeHeaderList(QuicStreamId stream_id,
                               const spdy::SpdyHeaderBlock* header_list);

  // Decode data received on the decoder stream.
  void DecodeDecoderStreamData(QuicStringPiece data);

  // QpackDecoderStreamReceiver::Delegate implementation
  void OnInsertCountIncrement(uint64_t increment) override;
  void OnHeaderAcknowledgement(QuicStreamId stream_id) override;
  void OnStreamCancellation(QuicStreamId stream_id) override;
  void OnErrorDetected(QuicStringPiece error_message) override;

 private:
  // TODO(bnc): Consider moving this class to QpackInstructionEncoder or
  // qpack_constants, adding factory methods, one for each instruction, and
  // changing QpackInstructionEncoder::Encoder() to take an
  // InstructionWithValues struct instead of separate |instruction| and |values|
  // arguments.
  struct InstructionWithValues {
    // |instruction| is not owned.
    const QpackInstruction* instruction;
    QpackInstructionEncoder::Values values;
  };

  DecoderStreamErrorDelegate* const decoder_stream_error_delegate_;
  QpackDecoderStreamReceiver decoder_stream_receiver_;
  QpackEncoderStreamSender encoder_stream_sender_;
  QpackHeaderTable header_table_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_H_
