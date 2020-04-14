// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_progressive_decoder.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_index_conversions.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_instructions.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_required_insert_count.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QpackProgressiveDecoder::QpackProgressiveDecoder(
    QuicStreamId stream_id,
    BlockedStreamLimitEnforcer* enforcer,
    DecodingCompletedVisitor* visitor,
    QpackHeaderTable* header_table,
    HeadersHandlerInterface* handler)
    : stream_id_(stream_id),
      prefix_decoder_(
          std::make_unique<QpackInstructionDecoder>(QpackPrefixLanguage(),
                                                    this)),
      instruction_decoder_(QpackRequestStreamLanguage(), this),
      enforcer_(enforcer),
      visitor_(visitor),
      header_table_(header_table),
      handler_(handler),
      required_insert_count_(0),
      base_(0),
      required_insert_count_so_far_(0),
      prefix_decoded_(false),
      blocked_(false),
      decoding_(true),
      error_detected_(false),
      cancelled_(false) {}

QpackProgressiveDecoder::~QpackProgressiveDecoder() {
  if (blocked_ && !cancelled_) {
    header_table_->UnregisterObserver(required_insert_count_, this);
  }
}

void QpackProgressiveDecoder::Decode(quiche::QuicheStringPiece data) {
  DCHECK(decoding_);

  if (data.empty() || error_detected_) {
    return;
  }

  // Decode prefix byte by byte until the first (and only) instruction is
  // decoded.
  while (!prefix_decoded_) {
    DCHECK(!blocked_);

    if (!prefix_decoder_->Decode(data.substr(0, 1))) {
      return;
    }

    // |prefix_decoder_->Decode()| must return false if an error is detected.
    DCHECK(!error_detected_);

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

void QpackProgressiveDecoder::OnError(quiche::QuicheStringPiece error_message) {
  DCHECK(!error_detected_);

  error_detected_ = true;
  // Might destroy |this|.
  handler_->OnDecodingErrorDetected(error_message);
}

void QpackProgressiveDecoder::OnInsertCountReachedThreshold() {
  DCHECK(blocked_);

  // Clear |blocked_| before calling instruction_decoder_.Decode() below,
  // because that might destroy |this| and ~QpackProgressiveDecoder() needs to
  // know not to call UnregisterObserver().
  blocked_ = false;
  enforcer_->OnStreamUnblocked(stream_id_);

  if (!buffer_.empty()) {
    std::string buffer(std::move(buffer_));
    buffer_.clear();
    if (!instruction_decoder_.Decode(buffer)) {
      // |this| might be destroyed.
      return;
    }
  }

  if (!decoding_) {
    FinishDecoding();
  }
}

void QpackProgressiveDecoder::Cancel() {
  cancelled_ = true;
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

    header_table_->set_dynamic_table_entry_referenced();
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

  header_table_->set_dynamic_table_entry_referenced();
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

    header_table_->set_dynamic_table_entry_referenced();
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

  header_table_->set_dynamic_table_entry_referenced();
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
    if (!enforcer_->OnStreamBlocked(stream_id_)) {
      OnError("Limit on number of blocked streams exceeded.");
      return false;
    }
    blocked_ = true;
    header_table_->RegisterObserver(required_insert_count_, this);
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

  visitor_->OnDecodingCompleted(stream_id_, required_insert_count_);
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
