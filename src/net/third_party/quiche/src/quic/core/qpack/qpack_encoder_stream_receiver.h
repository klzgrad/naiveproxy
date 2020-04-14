// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_RECEIVER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_RECEIVER_H_

#include <cstdint>
#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instruction_decoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_stream_receiver.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// This class decodes data received on the encoder stream.
class QUIC_EXPORT_PRIVATE QpackEncoderStreamReceiver
    : public QpackInstructionDecoder::Delegate,
      public QpackStreamReceiver {
 public:
  // An interface for handling instructions decoded from the encoder stream, see
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5.2
  class QUIC_EXPORT_PRIVATE Delegate {
   public:
    virtual ~Delegate() = default;

    // 5.2.1. Insert With Name Reference
    virtual void OnInsertWithNameReference(bool is_static,
                                           uint64_t name_index,
                                           quiche::QuicheStringPiece value) = 0;
    // 5.2.2. Insert Without Name Reference
    virtual void OnInsertWithoutNameReference(
        quiche::QuicheStringPiece name,
        quiche::QuicheStringPiece value) = 0;
    // 5.2.3. Duplicate
    virtual void OnDuplicate(uint64_t index) = 0;
    // 5.2.4. Set Dynamic Table Capacity
    virtual void OnSetDynamicTableCapacity(uint64_t capacity) = 0;
    // Decoding error
    virtual void OnErrorDetected(quiche::QuicheStringPiece error_message) = 0;
  };

  explicit QpackEncoderStreamReceiver(Delegate* delegate);
  QpackEncoderStreamReceiver() = delete;
  QpackEncoderStreamReceiver(const QpackEncoderStreamReceiver&) = delete;
  QpackEncoderStreamReceiver& operator=(const QpackEncoderStreamReceiver&) =
      delete;
  ~QpackEncoderStreamReceiver() override = default;

  // Implements QpackStreamReceiver::Decode().
  // Decode data and call appropriate Delegate method after each decoded
  // instruction.  Once an error occurs, Delegate::OnErrorDetected() is called,
  // and all further data is ignored.
  void Decode(quiche::QuicheStringPiece data) override;

  // QpackInstructionDecoder::Delegate implementation.
  bool OnInstructionDecoded(const QpackInstruction* instruction) override;
  void OnError(quiche::QuicheStringPiece error_message) override;

 private:
  QpackInstructionDecoder instruction_decoder_;
  Delegate* const delegate_;

  // True if a decoding error has been detected.
  bool error_detected_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_RECEIVER_H_
