// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_progressive_decoder.h"

#include <algorithm>
#include <limits>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_index_conversions.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_required_insert_count.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"

namespace quic {

QpackProgressiveDecoder::QpackProgressiveDecoder(
    QuicStreamId stream_id,
    BlockedStreamLimitEnforcer* enforcer,
    QpackHeaderTable* header_table,
    QpackDecoderStreamSender* decoder_stream_sender,
    HeadersHandlerInterface* handler)
    : stream_id_(stream_id),
      prefix_decoder_(
          QuicMakeUnique<QpackInstructionDecoder>(QpackPrefixLanguage(), this)),
      instruction_decoder_(QpackRequestStreamLanguage(), this),
      enforcer_(enforcer),
      header_table_(header_table),
      decoder_stream_sender_(decoder_stream_sender),
      handler_(handler),
      required_insert_count_(0),
      base_(0),
      required_insert_count_so_far_(0),
      prefix_decoded_(false),
      blocked_(false),
      decoding_(true),
      error_detected_(false) {}

void QpackProgressiveDecoder::Decode(QuicStringPiece data) {
  DCHECK(decoding_);

  if (data.empty() || error_detected_) {
    return;
  }

  // Decode prefix byte by byte until the first (and only) instruction is
  // decoded.
  while (!prefix_decoded_) {
    DCHECK(!blocked_);

    prefix_decoder_->Decode(data.substr(0, 1));
    if (error_detected_) {
      return;
    }

    data = data.substr(1);
    if (data.empty()) {
      return;
    }
  }

  if (blocked_) {
    buffer_.append(data.data(), data.size());
  } else {
    DCHECK(buffer_.empty());

    instruction_decoder_.Decode(data);
  }
}

void QpackProgressiveDecoder::EndHeaderBlock() {
  DCHECK(decoding_);
  decoding_ = false;

  if (!blocked_) {
    FinishDecoding();
  }
}

bool QpackProgressiveDecoder::OnInstructionDecoded(
    const QpackInstruction* instruction) {
  if (instruction == QpackPrefixInstruction()) {
    return DoPrefixInstruction();
  }

  DCHECK(prefix_decoded_);
  DCHECK_LE(required_insert_count_, header_table_->inserted_entry_count());

  if (instruction == QpackIndexedHeaderFieldInstruction()) {
    return DoIndexedHeaderFieldInstruction();
  }
  if (instruction == QpackIndexedHeaderFieldPostBaseInstruction()) {
    return DoIndexedHeaderFieldPostBaseInstruction();
  }
  if (instruction == QpackLiteralHeaderFieldNameReferenceInstruction()) {
    return DoLiteralHeaderFieldNameReferenceInstruction();
  }
  if (instruction == QpackLiteralHeaderFieldPostBaseInstruction()) {
    return DoLiteralHeaderFieldPostBaseInstruction();
  }
  DCHECK_EQ(instruction, QpackLiteralHeaderFieldInstruction());
  return DoLiteralHeaderFieldInstruction();
}

void QpackProgressiveDecoder::OnError(QuicStringPiece error_message) {
  DCHECK(!error_detected_);

  error_detected_ = true;
  handler_->OnDecodingErrorDetected(error_message);
}

void QpackProgressiveDecoder::OnInsertCountReachedThreshold() {
  DCHECK(blocked_);

  if (!buffer_.empty()) {
    instruction_decoder_.Decode(buffer_);
    buffer_.clear();
  }

  blocked_ = false;
  enforcer_->OnStreamUnblocked(stream_id_);

  if (!decoding_) {
    FinishDecoding();
  }
}

bool QpackProgressiveDecoder::DoIndexedHeaderFieldInstruction() {
  if (!instruction_decoder_.s_bit()) {
    uint64_t absolute_index;
    if (!QpackRequestStreamRelativeIndexToAbsoluteIndex(
            instruction_decoder_.varint(), base_, &absolute_index)) {
      OnError("Invalid relative index.");
      return false;
    }

    if (absolute_index >= required_insert_count_) {
      OnError("Absolute Index must be smaller than Required Insert Count.");
      return false;
    }

    DCHECK_LT(absolute_index, std::numeric_limits<uint64_t>::max());
    required_insert_count_so_far_ =
        std::max(required_insert_count_so_far_, absolute_index + 1);

    auto entry =
        header_table_->LookupEntry(/* is_static = */ false, absolute_index);
    if (!entry) {
      OnError("Dynamic table entry already evicted.");
      return false;
    }

    handler_->OnHeaderDecoded(entry->name(), entry->value());
    return true;
  }

  auto entry = header_table_->LookupEntry(/* is_static = */ true,
                                          instruction_decoder_.varint());
  if (!entry) {
    OnError("Static table entry not found.");
    return false;
  }

  handler_->OnHeaderDecoded(entry->name(), entry->value());
  return true;
}

bool QpackProgressiveDecoder::DoIndexedHeaderFieldPostBaseInstruction() {
  uint64_t absolute_index;
  if (!QpackPostBaseIndexToAbsoluteIndex(instruction_decoder_.varint(), base_,
                                         &absolute_index)) {
    OnError("Invalid post-base index.");
    return false;
  }

  if (absolute_index >= required_insert_count_) {
    OnError("Absolute Index must be smaller than Required Insert Count.");
    return false;
  }

  DCHECK_LT(absolute_index, std::numeric_limits<uint64_t>::max());
  required_insert_count_so_far_ =
      std::max(required_insert_count_so_far_, absolute_index + 1);

  auto entry =
      header_table_->LookupEntry(/* is_static = */ false, absolute_index);
  if (!entry) {
    OnError("Dynamic table entry already evicted.");
    return false;
  }

  handler_->OnHeaderDecoded(entry->name(), entry->value());
  return true;
}

bool QpackProgressiveDecoder::DoLiteralHeaderFieldNameReferenceInstruction() {
  if (!instruction_decoder_.s_bit()) {
    uint64_t absolute_index;
    if (!QpackRequestStreamRelativeIndexToAbsoluteIndex(
            instruction_decoder_.varint(), base_, &absolute_index)) {
      OnError("Invalid relative index.");
      return false;
    }

    if (absolute_index >= required_insert_count_) {
      OnError("Absolute Index must be smaller than Required Insert Count.");
      return false;
    }

    DCHECK_LT(absolute_index, std::numeric_limits<uint64_t>::max());
    required_insert_count_so_far_ =
        std::max(required_insert_count_so_far_, absolute_index + 1);

    auto entry =
        header_table_->LookupEntry(/* is_static = */ false, absolute_index);
    if (!entry) {
      OnError("Dynamic table entry already evicted.");
      return false;
    }

    handler_->OnHeaderDecoded(entry->name(), instruction_decoder_.value());
    return true;
  }

  auto entry = header_table_->LookupEntry(/* is_static = */ true,
                                          instruction_decoder_.varint());
  if (!entry) {
    OnError("Static table entry not found.");
    return false;
  }

  handler_->OnHeaderDecoded(entry->name(), instruction_decoder_.value());
  return true;
}

bool QpackProgressiveDecoder::DoLiteralHeaderFieldPostBaseInstruction() {
  uint64_t absolute_index;
  if (!QpackPostBaseIndexToAbsoluteIndex(instruction_decoder_.varint(), base_,
                                         &absolute_index)) {
    OnError("Invalid post-base index.");
    return false;
  }

  if (absolute_index >= required_insert_count_) {
    OnError("Absolute Index must be smaller than Required Insert Count.");
    return false;
  }

  DCHECK_LT(absolute_index, std::numeric_limits<uint64_t>::max());
  required_insert_count_so_far_ =
      std::max(required_insert_count_so_far_, absolute_index + 1);

  auto entry =
      header_table_->LookupEntry(/* is_static = */ false, absolute_index);
  if (!entry) {
    OnError("Dynamic table entry already evicted.");
    return false;
  }

  handler_->OnHeaderDecoded(entry->name(), instruction_decoder_.value());
  return true;
}

bool QpackProgressiveDecoder::DoLiteralHeaderFieldInstruction() {
  handler_->OnHeaderDecoded(instruction_decoder_.name(),
                            instruction_decoder_.value());

  return true;
}

bool QpackProgressiveDecoder::DoPrefixInstruction() {
  DCHECK(!prefix_decoded_);

  if (!QpackDecodeRequiredInsertCount(
          prefix_decoder_->varint(), header_table_->max_entries(),
          header_table_->inserted_entry_count(), &required_insert_count_)) {
    OnError("Error decoding Required Insert Count.");
    return false;
  }

  const bool sign = prefix_decoder_->s_bit();
  const uint64_t delta_base = prefix_decoder_->varint2();
  if (!DeltaBaseToBase(sign, delta_base, &base_)) {
    OnError("Error calculating Base.");
    return false;
  }

  prefix_decoded_ = true;

  if (required_insert_count_ > header_table_->inserted_entry_count()) {
    blocked_ = true;
    if (!enforcer_->OnStreamBlocked(stream_id_)) {
      OnError("Limit on number of blocked streams exceeded.");
      return false;
    }
    header_table_->RegisterObserver(this, required_insert_count_);
  }

  return true;
}

void QpackProgressiveDecoder::FinishDecoding() {
  DCHECK(buffer_.empty());
  DCHECK(!blocked_);
  DCHECK(!decoding_);

  if (error_detected_) {
    return;
  }

  if (!instruction_decoder_.AtInstructionBoundary()) {
    OnError("Incomplete header block.");
    return;
  }

  if (!prefix_decoded_) {
    OnError("Incomplete header data prefix.");
    return;
  }

  if (required_insert_count_ != required_insert_count_so_far_) {
    OnError("Required Insert Count too large.");
    return;
  }

  if (required_insert_count_ > 0) {
    decoder_stream_sender_->SendHeaderAcknowledgement(stream_id_);
  }

  handler_->OnDecodingCompleted();
}

bool QpackProgressiveDecoder::DeltaBaseToBase(bool sign,
                                              uint64_t delta_base,
                                              uint64_t* base) {
  if (sign) {
    if (delta_base == std::numeric_limits<uint64_t>::max() ||
        required_insert_count_ < delta_base + 1) {
      return false;
    }
    *base = required_insert_count_ - delta_base - 1;
    return true;
  }

  if (delta_base >
      std::numeric_limits<uint64_t>::max() - required_insert_count_) {
    return false;
  }
  *base = required_insert_count_ + delta_base;
  return true;
}

}  // namespace quic
