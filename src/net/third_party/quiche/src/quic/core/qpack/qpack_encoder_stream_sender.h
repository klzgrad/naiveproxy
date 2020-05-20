// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_SENDER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_SENDER_H_

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instruction_encoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_stream_sender_delegate.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// This class serializes instructions for transmission on the encoder stream.
// Serialized instructions are buffered until Flush() is called.
class QUIC_EXPORT_PRIVATE QpackEncoderStreamSender {
 public:
  QpackEncoderStreamSender();
  QpackEncoderStreamSender(const QpackEncoderStreamSender&) = delete;
  QpackEncoderStreamSender& operator=(const QpackEncoderStreamSender&) = delete;

  // Methods for serializing and buffering instructions, see
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5.2

  // 5.2.1. Insert With Name Reference
  void SendInsertWithNameReference(bool is_static,
                                   uint64_t name_index,
                                   quiche::QuicheStringPiece value);
  // 5.2.2. Insert Without Name Reference
  void SendInsertWithoutNameReference(quiche::QuicheStringPiece name,
                                      quiche::QuicheStringPiece value);
  // 5.2.3. Duplicate
  void SendDuplicate(uint64_t index);
  // 5.2.4. Set Dynamic Table Capacity
  void SendSetDynamicTableCapacity(uint64_t capacity);

  // Writes all buffered instructions on the encoder stream.
  // Returns the number of bytes written.
  QuicByteCount Flush();

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

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_SENDER_H_
