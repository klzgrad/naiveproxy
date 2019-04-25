// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_RECEIVER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_RECEIVER_H_

#include <cstdint>

#include "net/third_party/quic/core/qpack/qpack_instruction_decoder.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

// This class decodes data received on the decoder stream,
// and passes it along to its Delegate.
class QUIC_EXPORT_PRIVATE QpackDecoderStreamReceiver
    : public QpackInstructionDecoder::Delegate {
 public:
  // An interface for handling instructions decoded from the decoder stream, see
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5.3
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // 5.3.1 Insert Count Increment
    virtual void OnInsertCountIncrement(uint64_t increment) = 0;
    // 5.3.2 Header Acknowledgement
    virtual void OnHeaderAcknowledgement(QuicStreamId stream_id) = 0;
    // 5.3.3 Stream Cancellation
    virtual void OnStreamCancellation(QuicStreamId stream_id) = 0;
    // Decoding error
    virtual void OnErrorDetected(QuicStringPiece error_message) = 0;
  };

  explicit QpackDecoderStreamReceiver(Delegate* delegate);
  QpackDecoderStreamReceiver() = delete;
  QpackDecoderStreamReceiver(const QpackDecoderStreamReceiver&) = delete;
  QpackDecoderStreamReceiver& operator=(const QpackDecoderStreamReceiver&) =
      delete;

  // Decode data and call appropriate Delegate method after each decoded
  // instruction.  Once an error occurs, Delegate::OnErrorDetected() is called,
  // and all further data is ignored.
  void Decode(QuicStringPiece data);

  // QpackInstructionDecoder::Delegate implementation.
  bool OnInstructionDecoded(const QpackInstruction* instruction) override;
  void OnError(QuicStringPiece error_message) override;

 private:
  QpackInstructionDecoder instruction_decoder_;
  Delegate* const delegate_;

  // True if a decoding error has been detected.
  bool error_detected_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_STREAM_RECEIVER_H_
