// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/qpack_progressive_decoder.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/qpack/qpack_index_conversions.h"
#include "quiche/quic/core/qpack/qpack_instructions.h"
#include "quiche/quic/core/qpack/qpack_required_insert_count.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

namespace {

// The value argument passed to OnHeaderDecoded() is from an entry in the static
// table.
constexpr bool kValueFromStaticTable = true;

}  // anonymous namespace

QpackProgressiveDecoder::QpackProgressiveDecoder(
    QuicStreamId stream_id, BlockedStreamLimitEnforcer* enforcer,
    DecodingCompletedVisitor* visitor, QpackDecoderHeaderTable* header_table,
    HeadersHandlerInterface* handler)
    : stream_id_(stream_id),
      prefix_decoder_(std::make_unique<QpackInstructionDecoder>(
          QpackPrefixLanguage(), this)),
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

void QpackProgressiveDecoder::Decode(absl::string_view data) {
  QUICHE_DCHECK(decoding_);

  if (data.empty() || error_detected_) {
    return;
  }

  // Decode prefix byte by byte until the first (and only) instruction is
  // decoded.
  while (!prefix_decoded_) {
    QUICHE_DCHECK(!blocked_);

    if (!prefix_decoder_->Decode(data.substr(0, 1))) {
      return;
    }

    // |prefix_decoder_->Decode()| must return false if an error is detected.
    QUICHE_DCHECK(!error_detected_);

    data = data.substr(1);
    if (data.empty()) {
      return;
    }
  }

  if (blocked_) {
    buffer_.append(data.data(), data.size());
  } else {
    QUICHE_DCHECK(buffer_.empty());

    instruction_decoder_.Decode(data);
  }
}

void QpackProgressiveDecoder::EndHeaderBlock() {
  QUICHE_DCHECK(decoding_);
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

  QUICHE_DCHECK(prefix_decoded_);
  QUICHE_DCHECK_LE(required_insert_count_,
                   header_table_->inserted_entry_count());

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
  QUICHE_DCHECK_EQ(instruction, QpackLiteralHeaderFieldInstruction());
  return DoLiteralHeaderFieldInstruction();
}

void QpackProgressiveDecoder::OnInstructionDecodingError(
    QpackInstructionDecoder::ErrorCode /* error_code */,
    absl::string_view error_message) {
  // Ignore |error_code| and always use QUIC_QPACK_DECOMPRESSION_FAILED to avoid
  // having to define a new QuicErrorCode for every instruction decoder error.
  OnError(QUIC_QPACK_DECOMPRESSION_FAILED, error_message);
}

void QpackProgressiveDecoder::OnInsertCountReachedThreshold() {
  QUICHE_DCHECK(blocked_);

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

void QpackProgressiveDecoder::Cancel() { cancelled_ = true; }

bool QpackProgressiveDecoder::DoIndexedHeaderFieldInstruction() {
  if (!instruction_decoder_.s_bit()) {
    uint64_t absolute_index;
    if (!QpackRequestStreamRelativeIndexToAbsoluteIndex(
            instruction_decoder_.varint(), base_, &absolute_index)) {
      OnError(QUIC_QPACK_DECOMPRESSION_FAILED, "Invalid relative index.");
      return false;
    }

    if (absolute_index >= required_insert_count_) {
      OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
              "Absolute Index must be smaller than Required Insert Count.");
      return false;
    }

    QUICHE_DCHECK_LT(absolute_index, std::numeric_limits<uint64_t>::max());
    required_insert_count_so_far_ =
        std::max(required_insert_count_so_far_, absolute_index + 1);

    auto entry =
        header_table_->LookupEntry(/* is_static = */ false, absolute_index);
    if (!entry) {
      OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
              "Dynamic table entry already evicted.");
      return false;
    }

    header_table_->set_dynamic_table_entry_referenced();
    return OnHeaderDecoded(!kValueFromStaticTable, entry->name(),
                           entry->value());
  }

  auto entry = header_table_->LookupEntry(/* is_static = */ true,
                                          instruction_decoder_.varint());
  if (!entry) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED, "Static table entry not found.");
    return false;
  }

  return OnHeaderDecoded(kValueFromStaticTable, entry->name(), entry->value());
}

bool QpackProgressiveDecoder::DoIndexedHeaderFieldPostBaseInstruction() {
  uint64_t absolute_index;
  if (!QpackPostBaseIndexToAbsoluteIndex(instruction_decoder_.varint(), base_,
                                         &absolute_index)) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED, "Invalid post-base index.");
    return false;
  }

  if (absolute_index >= required_insert_count_) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
            "Absolute Index must be smaller than Required Insert Count.");
    return false;
  }

  QUICHE_DCHECK_LT(absolute_index, std::numeric_limits<uint64_t>::max());
  required_insert_count_so_far_ =
      std::max(required_insert_count_so_far_, absolute_index + 1);

  auto entry =
      header_table_->LookupEntry(/* is_static = */ false, absolute_index);
  if (!entry) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
            "Dynamic table entry already evicted.");
    return false;
  }

  header_table_->set_dynamic_table_entry_referenced();
  return OnHeaderDecoded(!kValueFromStaticTable, entry->name(), entry->value());
}

bool QpackProgressiveDecoder::DoLiteralHeaderFieldNameReferenceInstruction() {
  if (!instruction_decoder_.s_bit()) {
    uint64_t absolute_index;
    if (!QpackRequestStreamRelativeIndexToAbsoluteIndex(
            instruction_decoder_.varint(), base_, &absolute_index)) {
      OnError(QUIC_QPACK_DECOMPRESSION_FAILED, "Invalid relative index.");
      return false;
    }

    if (absolute_index >= required_insert_count_) {
      OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
              "Absolute Index must be smaller than Required Insert Count.");
      return false;
    }

    QUICHE_DCHECK_LT(absolute_index, std::numeric_limits<uint64_t>::max());
    required_insert_count_so_far_ =
        std::max(required_insert_count_so_far_, absolute_index + 1);

    auto entry =
        header_table_->LookupEntry(/* is_static = */ false, absolute_index);
    if (!entry) {
      OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
              "Dynamic table entry already evicted.");
      return false;
    }

    header_table_->set_dynamic_table_entry_referenced();
    return OnHeaderDecoded(!kValueFromStaticTable, entry->name(),
                           instruction_decoder_.value());
  }

  auto entry = header_table_->LookupEntry(/* is_static = */ true,
                                          instruction_decoder_.varint());
  if (!entry) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED, "Static table entry not found.");
    return false;
  }

  return OnHeaderDecoded(kValueFromStaticTable, entry->name(),
                         instruction_decoder_.value());
}

bool QpackProgressiveDecoder::DoLiteralHeaderFieldPostBaseInstruction() {
  uint64_t absolute_index;
  if (!QpackPostBaseIndexToAbsoluteIndex(instruction_decoder_.varint(), base_,
                                         &absolute_index)) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED, "Invalid post-base index.");
    return false;
  }

  if (absolute_index >= required_insert_count_) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
            "Absolute Index must be smaller than Required Insert Count.");
    return false;
  }

  QUICHE_DCHECK_LT(absolute_index, std::numeric_limits<uint64_t>::max());
  required_insert_count_so_far_ =
      std::max(required_insert_count_so_far_, absolute_index + 1);

  auto entry =
      header_table_->LookupEntry(/* is_static = */ false, absolute_index);
  if (!entry) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
            "Dynamic table entry already evicted.");
    return false;
  }

  header_table_->set_dynamic_table_entry_referenced();
  return OnHeaderDecoded(!kValueFromStaticTable, entry->name(),
                         instruction_decoder_.value());
}

bool QpackProgressiveDecoder::DoLiteralHeaderFieldInstruction() {
  return OnHeaderDecoded(!kValueFromStaticTable, instruction_decoder_.name(),
                         instruction_decoder_.value());
}

bool QpackProgressiveDecoder::DoPrefixInstruction() {
  QUICHE_DCHECK(!prefix_decoded_);

  if (!QpackDecodeRequiredInsertCount(
          prefix_decoder_->varint(), header_table_->max_entries(),
          header_table_->inserted_entry_count(), &required_insert_count_)) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
            "Error decoding Required Insert Count.");
    return false;
  }

  const bool sign = prefix_decoder_->s_bit();
  const uint64_t delta_base = prefix_decoder_->varint2();
  if (!DeltaBaseToBase(sign, delta_base, &base_)) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED, "Error calculating Base.");
    return false;
  }

  prefix_decoded_ = true;

  if (required_insert_count_ > header_table_->inserted_entry_count()) {
    if (!enforcer_->OnStreamBlocked(stream_id_)) {
      OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
              "Limit on number of blocked streams exceeded.");
      return false;
    }
    blocked_ = true;
    header_table_->RegisterObserver(required_insert_count_, this);
  }

  return true;
}

bool QpackProgressiveDecoder::OnHeaderDecoded(bool /*value_from_static_table*/,
                                              absl::string_view name,
                                              absl::string_view value) {
  handler_->OnHeaderDecoded(name, value);
  return true;
}

void QpackProgressiveDecoder::FinishDecoding() {
  QUICHE_DCHECK(buffer_.empty());
  QUICHE_DCHECK(!blocked_);
  QUICHE_DCHECK(!decoding_);

  if (error_detected_) {
    return;
  }

  if (!instruction_decoder_.AtInstructionBoundary()) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED, "Incomplete header block.");
    return;
  }

  if (!prefix_decoded_) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED, "Incomplete header data prefix.");
    return;
  }

  if (required_insert_count_ != required_insert_count_so_far_) {
    OnError(QUIC_QPACK_DECOMPRESSION_FAILED,
            "Required Insert Count too large.");
    return;
  }

  visitor_->OnDecodingCompleted(stream_id_, required_insert_count_);
  handler_->OnDecodingCompleted();
}

void QpackProgressiveDecoder::OnError(QuicErrorCode error_code,
                                      absl::string_view error_message) {
  QUICHE_DCHECK(!error_detected_);

  error_detected_ = true;
  // Might destroy |this|.
  handler_->OnDecodingErrorDetected(error_code, error_message);
}

bool QpackProgressiveDecoder::DeltaBaseToBase(bool sign, uint64_t delta_base,
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
