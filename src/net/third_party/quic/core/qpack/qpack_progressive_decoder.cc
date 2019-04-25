// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_progressive_decoder.h"

#include <algorithm>
#include <limits>

#include "base/logging.h"
#include "net/third_party/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"

namespace quic {

QpackProgressiveDecoder::QpackProgressiveDecoder(
    QuicStreamId stream_id,
    QpackHeaderTable* header_table,
    QpackDecoderStreamSender* decoder_stream_sender,
    HeadersHandlerInterface* handler)
    : stream_id_(stream_id),
      prefix_decoder_(
          QuicMakeUnique<QpackInstructionDecoder>(QpackPrefixLanguage(), this)),
      instruction_decoder_(QpackRequestStreamLanguage(), this),
      header_table_(header_table),
      decoder_stream_sender_(decoder_stream_sender),
      handler_(handler),
      required_insert_count_(0),
      base_(0),
      required_insert_count_so_far_(0),
      prefix_decoded_(false),
      decoding_(true),
      error_detected_(false) {}

// static
bool QpackProgressiveDecoder::DecodeRequiredInsertCount(
    uint64_t encoded_required_insert_count,
    uint64_t max_entries,
    uint64_t total_number_of_inserts,
    uint64_t* required_insert_count) {
  if (encoded_required_insert_count == 0) {
    *required_insert_count = 0;
    return true;
  }

  // |max_entries| is calculated by dividing an unsigned 64-bit integer by 32,
  // precluding all calculations in this method from overflowing.
  DCHECK_LE(max_entries, std::numeric_limits<uint64_t>::max() / 32);

  if (encoded_required_insert_count > 2 * max_entries) {
    return false;
  }

  *required_insert_count = encoded_required_insert_count - 1;
  DCHECK_LT(*required_insert_count, std::numeric_limits<uint64_t>::max() / 16);

  uint64_t current_wrapped = total_number_of_inserts % (2 * max_entries);
  DCHECK_LT(current_wrapped, std::numeric_limits<uint64_t>::max() / 16);

  if (current_wrapped >= *required_insert_count + max_entries) {
    // Required Insert Count wrapped around 1 extra time.
    *required_insert_count += 2 * max_entries;
  } else if (current_wrapped + max_entries < *required_insert_count) {
    // Decoder wrapped around 1 extra time.
    current_wrapped += 2 * max_entries;
  }

  if (*required_insert_count >
      std::numeric_limits<uint64_t>::max() - total_number_of_inserts) {
    return false;
  }

  *required_insert_count += total_number_of_inserts;

  // Prevent underflow, also disallow invalid value 0 for Required Insert Count.
  if (current_wrapped >= *required_insert_count) {
    return false;
  }

  *required_insert_count -= current_wrapped;

  return true;
}

void QpackProgressiveDecoder::Decode(QuicStringPiece data) {
  DCHECK(decoding_);

  if (data.empty() || error_detected_) {
    return;
  }

  // Decode prefix byte by byte until the first (and only) instruction is
  // decoded.
  while (!prefix_decoded_) {
    prefix_decoder_->Decode(data.substr(0, 1));
    data = data.substr(1);
    if (data.empty()) {
      return;
    }
  }

  instruction_decoder_.Decode(data);
}

void QpackProgressiveDecoder::EndHeaderBlock() {
  DCHECK(decoding_);
  decoding_ = false;

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

  decoder_stream_sender_->SendHeaderAcknowledgement(stream_id_);
  handler_->OnDecodingCompleted();
}

bool QpackProgressiveDecoder::OnInstructionDecoded(
    const QpackInstruction* instruction) {
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
  if (instruction == QpackLiteralHeaderFieldInstruction()) {
    return DoLiteralHeaderFieldInstruction();
  }
  DCHECK_EQ(instruction, QpackPrefixInstruction());
  return DoPrefixInstruction();
}

void QpackProgressiveDecoder::OnError(QuicStringPiece error_message) {
  DCHECK(!error_detected_);

  error_detected_ = true;
  handler_->OnDecodingErrorDetected(error_message);
}

bool QpackProgressiveDecoder::DoIndexedHeaderFieldInstruction() {
  if (!instruction_decoder_.s_bit()) {
    uint64_t absolute_index;
    if (!RequestStreamRelativeIndexToAbsoluteIndex(
            instruction_decoder_.varint(), &absolute_index)) {
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
      OnError("Dynamic table entry not found.");
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
  if (!PostBaseIndexToAbsoluteIndex(instruction_decoder_.varint(),
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
    OnError("Dynamic table entry not found.");
    return false;
  }

  handler_->OnHeaderDecoded(entry->name(), entry->value());
  return true;
}

bool QpackProgressiveDecoder::DoLiteralHeaderFieldNameReferenceInstruction() {
  if (!instruction_decoder_.s_bit()) {
    uint64_t absolute_index;
    if (!RequestStreamRelativeIndexToAbsoluteIndex(
            instruction_decoder_.varint(), &absolute_index)) {
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
      OnError("Dynamic table entry not found.");
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
  if (!PostBaseIndexToAbsoluteIndex(instruction_decoder_.varint(),
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
    OnError("Dynamic table entry not found.");
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

  if (!DecodeRequiredInsertCount(
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

  return true;
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

bool QpackProgressiveDecoder::RequestStreamRelativeIndexToAbsoluteIndex(
    uint64_t relative_index,
    uint64_t* absolute_index) const {
  if (relative_index == std::numeric_limits<uint64_t>::max() ||
      relative_index + 1 > base_) {
    return false;
  }

  *absolute_index = base_ - 1 - relative_index;
  return true;
}

bool QpackProgressiveDecoder::PostBaseIndexToAbsoluteIndex(
    uint64_t post_base_index,
    uint64_t* absolute_index) const {
  if (post_base_index >= std::numeric_limits<uint64_t>::max() - base_) {
    return false;
  }

  *absolute_index = base_ + post_base_index;
  return true;
}

}  // namespace quic
