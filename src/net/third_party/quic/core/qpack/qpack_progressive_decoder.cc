// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_progressive_decoder.h"

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
      largest_reference_(0),
      base_index_(0),
      largest_reference_seen_(0),
      prefix_decoded_(false),
      decoding_(true),
      error_detected_(false) {}

// static
bool QpackProgressiveDecoder::DecodeLargestReference(
    uint64_t wire_largest_reference,
    uint64_t max_entries,
    uint64_t total_number_of_inserts,
    uint64_t* largest_reference) {
  if (wire_largest_reference == 0) {
    *largest_reference = 0;
    return true;
  }

  // |max_entries| is calculated by dividing an unsigned 64-bit integer by 32,
  // precluding all calculations in this method from overflowing.
  DCHECK_LE(max_entries, std::numeric_limits<uint64_t>::max() / 32);

  if (wire_largest_reference > 2 * max_entries) {
    return false;
  }

  *largest_reference = wire_largest_reference - 1;
  DCHECK_LT(*largest_reference, std::numeric_limits<uint64_t>::max() / 16);

  uint64_t current_wrapped = total_number_of_inserts % (2 * max_entries);
  DCHECK_LT(current_wrapped, std::numeric_limits<uint64_t>::max() / 16);

  if (current_wrapped >= *largest_reference + max_entries) {
    // Largest Reference wrapped around 1 extra time.
    *largest_reference += 2 * max_entries;
  } else if (current_wrapped + max_entries < *largest_reference) {
    // Decoder wrapped around 1 extra time.
    current_wrapped += 2 * max_entries;
  }

  if (*largest_reference >
      std::numeric_limits<uint64_t>::max() - total_number_of_inserts) {
    return false;
  }

  *largest_reference += total_number_of_inserts;

  // Prevent underflow, but also disallow invalid value 0 for Largest Reference.
  if (current_wrapped >= *largest_reference) {
    return false;
  }

  *largest_reference -= current_wrapped;

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

  if (largest_reference_ != largest_reference_seen_) {
    OnError("Largest Reference too large.");
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

    if (absolute_index > largest_reference_) {
      OnError("Index larger than Largest Reference.");
      return false;
    }

    largest_reference_seen_ = std::max(largest_reference_seen_, absolute_index);

    DCHECK_NE(0u, absolute_index);
    const uint64_t real_index = absolute_index - 1;
    auto entry =
        header_table_->LookupEntry(/* is_static = */ false, real_index);
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

  if (absolute_index > largest_reference_) {
    OnError("Index larger than Largest Reference.");
    return false;
  }

  largest_reference_seen_ = std::max(largest_reference_seen_, absolute_index);

  DCHECK_NE(0u, absolute_index);
  const uint64_t real_index = absolute_index - 1;
  auto entry = header_table_->LookupEntry(/* is_static = */ false, real_index);
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

    if (absolute_index > largest_reference_) {
      OnError("Index larger than Largest Reference.");
      return false;
    }

    largest_reference_seen_ = std::max(largest_reference_seen_, absolute_index);

    DCHECK_NE(0u, absolute_index);
    const uint64_t real_index = absolute_index - 1;
    auto entry =
        header_table_->LookupEntry(/* is_static = */ false, real_index);
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

  if (absolute_index > largest_reference_) {
    OnError("Index larger than Largest Reference.");
    return false;
  }

  largest_reference_seen_ = std::max(largest_reference_seen_, absolute_index);

  DCHECK_NE(0u, absolute_index);
  const uint64_t real_index = absolute_index - 1;
  auto entry = header_table_->LookupEntry(/* is_static = */ false, real_index);
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

  if (!DecodeLargestReference(
          prefix_decoder_->varint(), header_table_->max_entries(),
          header_table_->inserted_entry_count(), &largest_reference_)) {
    OnError("Error decoding Largest Reference.");
    return false;
  }

  const bool sign = prefix_decoder_->s_bit();
  const uint64_t delta_base_index = prefix_decoder_->varint2();
  if (!DeltaBaseIndexToBaseIndex(sign, delta_base_index, &base_index_)) {
    OnError("Error calculating Base Index.");
    return false;
  }

  prefix_decoded_ = true;

  return true;
}

bool QpackProgressiveDecoder::DeltaBaseIndexToBaseIndex(
    bool sign,
    uint64_t delta_base_index,
    uint64_t* base_index) {
  if (sign) {
    if (delta_base_index == std::numeric_limits<uint64_t>::max() ||
        largest_reference_ < delta_base_index + 1) {
      return false;
    }
    *base_index = largest_reference_ - delta_base_index - 1;
    return true;
  }

  if (delta_base_index >
      std::numeric_limits<uint64_t>::max() - largest_reference_) {
    return false;
  }
  *base_index = largest_reference_ + delta_base_index;
  return true;
}

bool QpackProgressiveDecoder::RequestStreamRelativeIndexToAbsoluteIndex(
    uint64_t relative_index,
    uint64_t* absolute_index) const {
  if (relative_index == std::numeric_limits<uint64_t>::max() ||
      relative_index + 1 > base_index_) {
    return false;
  }

  *absolute_index = base_index_ - relative_index;
  return true;
}

bool QpackProgressiveDecoder::PostBaseIndexToAbsoluteIndex(
    uint64_t post_base_index,
    uint64_t* absolute_index) const {
  if (post_base_index >= std::numeric_limits<uint64_t>::max() - base_index_) {
    return false;
  }

  *absolute_index = base_index_ + post_base_index + 1;
  return true;
}

}  // namespace quic
