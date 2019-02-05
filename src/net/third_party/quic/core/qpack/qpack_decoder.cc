// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_decoder.h"

#include "base/logging.h"
#include "net/third_party/http2/decoder/decode_buffer.h"
#include "net/third_party/http2/decoder/decode_status.h"
#include "net/third_party/quic/core/qpack/qpack_constants.h"

namespace quic {

QpackDecoder::ProgressiveDecoder::ProgressiveDecoder(
    QpackHeaderTable* header_table,
    QpackDecoder::HeadersHandlerInterface* handler)
    : instruction_decoder_(QpackRequestStreamLanguage(), this),
      header_table_(header_table),
      handler_(handler),
      decoding_(true),
      error_detected_(false) {}

void QpackDecoder::ProgressiveDecoder::Decode(QuicStringPiece data) {
  DCHECK(decoding_);

  if (data.empty() || error_detected_) {
    return;
  }

  instruction_decoder_.Decode(data);
}

void QpackDecoder::ProgressiveDecoder::EndHeaderBlock() {
  DCHECK(decoding_);
  decoding_ = false;

  if (error_detected_) {
    return;
  }

  if (instruction_decoder_.AtInstructionBoundary()) {
    handler_->OnDecodingCompleted();
  } else {
    OnError("Incomplete header block.");
  }
}

bool QpackDecoder::ProgressiveDecoder::OnInstructionDecoded(
    const QpackInstruction* instruction) {
  if (instruction == QpackIndexedHeaderFieldInstruction()) {
    if (!instruction_decoder_.is_static()) {
      // TODO(bnc): Implement.
      OnError("Indexed Header Field with dynamic entry not implemented.");
      return false;
    }

    auto entry = header_table_->LookupEntry(instruction_decoder_.varint());
    if (!entry) {
      OnError("Invalid static table index.");
      return false;
    }

    handler_->OnHeaderDecoded(entry->name(), entry->value());
    return true;
  }

  if (instruction == QpackIndexedHeaderFieldPostBaseInstruction()) {
    // TODO(bnc): Implement.
    OnError("Indexed Header Field With Post-Base Index not implemented.");
    return false;
  }

  if (instruction == QpackLiteralHeaderFieldNameReferenceInstruction()) {
    if (!instruction_decoder_.is_static()) {
      // TODO(bnc): Implement.
      OnError(
          "Literal Header Field With Name Reference with dynamic entry not "
          "implemented.");
      return false;
    }

    auto entry = header_table_->LookupEntry(instruction_decoder_.varint());
    if (!entry) {
      OnError(
          "Invalid static table index in Literal Header Field With Name "
          "Reference instruction.");
      return false;
    }

    handler_->OnHeaderDecoded(entry->name(), instruction_decoder_.value());
    return true;
  }

  if (instruction == QpackLiteralHeaderFieldPostBaseInstruction()) {
    // TODO(bnc): Implement.
    OnError(
        "Literal Header Field With Post-Base Name Reference not "
        "implemented.");
    return false;
  }

  DCHECK_EQ(instruction, QpackLiteralHeaderFieldInstruction());

  handler_->OnHeaderDecoded(instruction_decoder_.name(),
                            instruction_decoder_.value());

  return true;
}

void QpackDecoder::ProgressiveDecoder::OnError(QuicStringPiece error_message) {
  DCHECK(!error_detected_);

  error_detected_ = true;
  handler_->OnDecodingErrorDetected(error_message);
}

std::unique_ptr<QpackDecoder::ProgressiveDecoder>
QpackDecoder::DecodeHeaderBlock(
    QpackDecoder::HeadersHandlerInterface* handler) {
  return std::make_unique<ProgressiveDecoder>(&header_table_, handler);
}

}  // namespace quic
