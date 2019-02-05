// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_RECEIVER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_RECEIVER_H_

#include <cstddef>
#include <cstdint>

#include "net/third_party/quic/core/qpack/qpack_instruction_decoder.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

// This class decodes data received on the encoder stream.
class QUIC_EXPORT_PRIVATE QpackEncoderStreamReceiver
    : public QpackInstructionDecoder::Delegate {
 public:
  // An interface for handling instructions decoded from the encoder stream, see
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5.2
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // 5.2.1. Insert With Name Reference
    virtual void OnInsertWithNameReference(bool is_static,
                                           uint64_t name_index,
                                           QuicStringPiece value) = 0;
    // 5.2.2. Insert Without Name Reference
    virtual void OnInsertWithoutNameReference(QuicStringPiece name,
                                              QuicStringPiece value) = 0;
    // 5.2.3. Duplicate
    virtual void OnDuplicate(uint64_t index) = 0;
    // 5.2.4. Dynamic Table Size Update
    virtual void OnDynamicTableSizeUpdate(uint64_t max_size) = 0;
    // Decoding error
    virtual void OnErrorDetected(QuicStringPiece error_message) = 0;
  };

  explicit QpackEncoderStreamReceiver(Delegate* delegate);
  QpackEncoderStreamReceiver() = delete;
  QpackEncoderStreamReceiver(const QpackEncoderStreamReceiver&) = delete;
  QpackEncoderStreamReceiver& operator=(const QpackEncoderStreamReceiver&) =
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

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_RECEIVER_H_
