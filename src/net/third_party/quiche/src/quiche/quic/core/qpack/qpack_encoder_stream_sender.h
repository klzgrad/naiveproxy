// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_SENDER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_SENDER_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/qpack/qpack_instruction_encoder.h"
#include "quiche/quic/core/qpack/qpack_stream_sender_delegate.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// This class serializes instructions for transmission on the encoder stream.
// Serialized instructions are buffered until Flush() is called.
class QUICHE_EXPORT QpackEncoderStreamSender {
 public:
  QpackEncoderStreamSender();
  QpackEncoderStreamSender(const QpackEncoderStreamSender&) = delete;
  QpackEncoderStreamSender& operator=(const QpackEncoderStreamSender&) = delete;

  // Methods for serializing and buffering instructions, see
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#rfc.section.5.2

  // 5.2.1. Insert With Name Reference
  void SendInsertWithNameReference(bool is_static, uint64_t name_index,
                                   absl::string_view value);
  // 5.2.2. Insert Without Name Reference
  void SendInsertWithoutNameReference(absl::string_view name,
                                      absl::string_view value);
  // 5.2.3. Duplicate
  void SendDuplicate(uint64_t index);
  // 5.2.4. Set Dynamic Table Capacity
  void SendSetDynamicTableCapacity(uint64_t capacity);

  // Returns number of bytes buffered by this object.
  // There is no limit on how much data this object is willing to buffer.
  QuicByteCount BufferedByteCount() const { return buffer_.size(); }

  // Returns whether writing to the encoder stream is allowed.  Writing is
  // disallowed if the amount of data buffered by the underlying stream exceeds
  // a hardcoded limit, in order to limit memory consumption in case the encoder
  // stream is blocked.  CanWrite() returning true does not mean that the
  // encoder stream is not blocked, it just means the blocked data does not
  // exceed the threshold.
  bool CanWrite() const;

  // Writes all buffered instructions on the encoder stream.
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

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_ENCODER_STREAM_SENDER_H_
