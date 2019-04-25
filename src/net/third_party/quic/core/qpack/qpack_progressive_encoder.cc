// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_progressive_encoder.h"

#include "base/logging.h"
#include "net/third_party/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

QpackProgressiveEncoder::QpackProgressiveEncoder(
    QuicStreamId stream_id,
    QpackHeaderTable* header_table,
    QpackEncoderStreamSender* encoder_stream_sender,
    const spdy::SpdyHeaderBlock* header_list)
    : stream_id_(stream_id),
      header_table_(header_table),
      encoder_stream_sender_(encoder_stream_sender),
      header_list_(header_list),
      header_list_iterator_(header_list_->begin()),
      prefix_encoded_(false) {
  // TODO(bnc): Use |stream_id_| for dynamic table entry management, and
  // remove this dummy DCHECK.
  DCHECK_LE(0u, stream_id_);

  DCHECK(header_table_);
  DCHECK(encoder_stream_sender_);
  DCHECK(header_list_);
}

bool QpackProgressiveEncoder::HasNext() const {
  return header_list_iterator_ != header_list_->end() || !prefix_encoded_;
}

void QpackProgressiveEncoder::Next(size_t max_encoded_bytes,
                                   QuicString* output) {
  DCHECK_NE(0u, max_encoded_bytes);
  DCHECK(HasNext());

  // Since QpackInstructionEncoder::Next() does not indicate the number of bytes
  // written, save the maximum new size of |*output|.
  const size_t max_length = output->size() + max_encoded_bytes;

  DCHECK_LT(output->size(), max_length);

  if (!prefix_encoded_ && !instruction_encoder_.HasNext()) {
    // TODO(bnc): Implement dynamic entries and set Required Insert Count and
    // Delta Base accordingly.
    instruction_encoder_.set_varint(0);
    instruction_encoder_.set_varint2(0);
    instruction_encoder_.set_s_bit(false);

    instruction_encoder_.Encode(QpackPrefixInstruction());

    DCHECK(instruction_encoder_.HasNext());
  }

  do {
    // Call QpackInstructionEncoder::Encode for |*header_list_iterator_| if it
    // has not been called yet.
    if (!instruction_encoder_.HasNext()) {
      DCHECK(prefix_encoded_);

      // Even after |name| and |value| go out of scope, copies of these
      // QuicStringPieces retained by QpackInstructionEncoder are still valid as
      // long as |header_list_| is valid.
      QuicStringPiece name = header_list_iterator_->first;
      QuicStringPiece value = header_list_iterator_->second;

      // |is_static| and |index| are saved by QpackInstructionEncoder by value,
      // there are no lifetime concerns.
      bool is_static;
      uint64_t index;

      auto match_type =
          header_table_->FindHeaderField(name, value, &is_static, &index);

      switch (match_type) {
        case QpackHeaderTable::MatchType::kNameAndValue:
          DCHECK(is_static) << "Dynamic table entries not supported yet.";

          instruction_encoder_.set_s_bit(is_static);
          instruction_encoder_.set_varint(index);

          instruction_encoder_.Encode(QpackIndexedHeaderFieldInstruction());

          break;
        case QpackHeaderTable::MatchType::kName:
          DCHECK(is_static) << "Dynamic table entries not supported yet.";

          instruction_encoder_.set_s_bit(is_static);
          instruction_encoder_.set_varint(index);
          instruction_encoder_.set_value(value);

          instruction_encoder_.Encode(
              QpackLiteralHeaderFieldNameReferenceInstruction());

          break;
        case QpackHeaderTable::MatchType::kNoMatch:
          instruction_encoder_.set_name(name);
          instruction_encoder_.set_value(value);

          instruction_encoder_.Encode(QpackLiteralHeaderFieldInstruction());

          break;
      }
    }

    DCHECK(instruction_encoder_.HasNext());

    instruction_encoder_.Next(max_length - output->size(), output);

    if (instruction_encoder_.HasNext()) {
      // There was not enough room to completely encode current header field.
      DCHECK_EQ(output->size(), max_length);

      return;
    }

    // It is possible that the output buffer was just large enough for encoding
    // the current header field, hence equality is allowed here.
    DCHECK_LE(output->size(), max_length);

    if (prefix_encoded_) {
      // Move on to the next header field.
      ++header_list_iterator_;
    } else {
      // Mark prefix as encoded.
      prefix_encoded_ = true;
    }
  } while (HasNext() && output->size() < max_length);
}

}  // namespace quic
