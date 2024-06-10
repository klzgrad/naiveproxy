// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_RECEIVER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_RECEIVER_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/qpack/qpack_instruction_decoder.h"
#include "quiche/quic/core/qpack/qpack_stream_receiver.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// This class decodes data received on the decoder stream,
// and passes it along to its Delegate.
class QUICHE_EXPORT QpackDecoderStreamReceiver
    : public QpackInstructionDecoder::Delegate,
      public QpackStreamReceiver {
 public:
  // An interface for handling instructions decoded from the decoder stream, see
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5.3
  class QUICHE_EXPORT Delegate {
   public:
    virtual ~Delegate() = default;

    // 5.3.1 Insert Count Increment
    virtual void OnInsertCountIncrement(uint64_t increment) = 0;
    // 5.3.2 Header Acknowledgement
    virtual void OnHeaderAcknowledgement(QuicStreamId stream_id) = 0;
    // 5.3.3 Stream Cancellation
    virtual void OnStreamCancellation(QuicStreamId stream_id) = 0;
    // Decoding error
    virtual void OnErrorDetected(QuicErrorCode error_code,
                                 absl::string_view error_message) = 0;
  };

  explicit QpackDecoderStreamReceiver(Delegate* delegate);
  QpackDecoderStreamReceiver() = delete;
  QpackDecoderStreamReceiver(const QpackDecoderStreamReceiver&) = delete;
  QpackDecoderStreamReceiver& operator=(const QpackDecoderStreamReceiver&) =
      delete;

  // Implements QpackStreamReceiver::Decode().
  // Decode data and call appropriate Delegate method after each decoded
  // instruction.  Once an error occurs, Delegate::OnErrorDetected() is called,
  // and all further data is ignored.
  void Decode(absl::string_view data) override;

  // QpackInstructionDecoder::Delegate implementation.
  bool OnInstructionDecoded(const QpackInstruction* instruction) override;
  void OnInstructionDecodingError(QpackInstructionDecoder::ErrorCode error_code,
                                  absl::string_view error_message) override;

 private:
  QpackInstructionDecoder instruction_decoder_;
  Delegate* const delegate_;

  // True if a decoding error has been detected.
  bool error_detected_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_RECEIVER_H_
