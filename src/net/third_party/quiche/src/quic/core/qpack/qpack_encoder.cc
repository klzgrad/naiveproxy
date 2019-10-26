// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder.h"

#include <algorithm>
#include <utility>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_index_conversions.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_instruction_encoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_required_insert_count.h"
#include "net/third_party/quiche/src/quic/core/qpack/value_splitting_header_list.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"

namespace quic {

namespace {

// Fraction to calculate draining index.  The oldest |kDrainingFraction| entries
// will not be referenced in header blocks.  A new entry (duplicate or literal
// with name reference) will be added to the dynamic table instead.  This allows
// the number of references to the draining entry to go to zero faster, so that
// it can be evicted.  See
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#avoiding-blocked-insertions.
// TODO(bnc): Fine tune.
const float kDrainingFraction = 0.25;

}  // anonymous namespace

QpackEncoder::QpackEncoder(
    DecoderStreamErrorDelegate* decoder_stream_error_delegate)
    : decoder_stream_error_delegate_(decoder_stream_error_delegate),
      decoder_stream_receiver_(this),
      maximum_blocked_streams_(0) {
  DCHECK(decoder_stream_error_delegate_);
}

QpackEncoder::~QpackEncoder() {}

// static
QpackEncoder::InstructionWithValues QpackEncoder::EncodeIndexedHeaderField(
    bool is_static,
    uint64_t index,
    QpackBlockingManager::IndexSet* referred_indices) {
  InstructionWithValues instruction{QpackIndexedHeaderFieldInstruction(), {}};
  instruction.values.s_bit = is_static;
  instruction.values.varint = index;
  // Add |index| to |*referred_indices| only if entry is in the dynamic table.
  if (!is_static) {
    referred_indices->insert(index);
  }
  return instruction;
}

// static
QpackEncoder::InstructionWithValues
QpackEncoder::EncodeLiteralHeaderFieldWithNameReference(
    bool is_static,
    uint64_t index,
    QuicStringPiece value,
    QpackBlockingManager::IndexSet* referred_indices) {
  InstructionWithValues instruction{
      QpackLiteralHeaderFieldNameReferenceInstruction(), {}};
  instruction.values.s_bit = is_static;
  instruction.values.varint = index;
  instruction.values.value = value;
  // Add |index| to |*referred_indices| only if entry is in the dynamic table.
  if (!is_static) {
    referred_indices->insert(index);
  }
  return instruction;
}

// static
QpackEncoder::InstructionWithValues QpackEncoder::EncodeLiteralHeaderField(
    QuicStringPiece name,
    QuicStringPiece value) {
  InstructionWithValues instruction{QpackLiteralHeaderFieldInstruction(), {}};
  instruction.values.name = name;
  instruction.values.value = value;
  return instruction;
}

QpackEncoder::Instructions QpackEncoder::FirstPassEncode(
    const spdy::SpdyHeaderBlock& header_list,
    QpackBlockingManager::IndexSet* referred_indices) {
  Instructions instructions;
  instructions.reserve(header_list.size());

  // The index of the oldest entry that must not be evicted.
  uint64_t smallest_blocking_index =
      blocking_manager_.smallest_blocking_index();
  // Entries with index larger than or equal to |known_received_count| are
  // blocking.
  const uint64_t known_received_count =
      blocking_manager_.known_received_count();
  // Only entries with index greater than or equal to |draining_index| are
  // allowed to be referenced.
  const uint64_t draining_index =
      header_table_.draining_index(kDrainingFraction);
  // Blocking references are allowed if the number of blocked streams is less
  // than the limit.
  // TODO(b/112770235): Also allow blocking if given stream is already blocked.
  const bool blocking_allowed =
      maximum_blocked_streams_ > blocking_manager_.blocked_stream_count();

  for (const auto& header : ValueSplittingHeaderList(&header_list)) {
    // These strings are owned by |header_list|.
    QuicStringPiece name = header.first;
    QuicStringPiece value = header.second;

    bool is_static;
    uint64_t index;

    auto match_type =
        header_table_.FindHeaderField(name, value, &is_static, &index);

    switch (match_type) {
      case QpackHeaderTable::MatchType::kNameAndValue:
        if (is_static) {
          // Refer to entry directly.
          instructions.push_back(
              EncodeIndexedHeaderField(is_static, index, referred_indices));

          break;
        }

        if (blocking_allowed || index < known_received_count) {
          if (index >= draining_index) {
            // If allowed, refer to entry directly.
            instructions.push_back(
                EncodeIndexedHeaderField(is_static, index, referred_indices));
            smallest_blocking_index = std::min(smallest_blocking_index, index);

            break;
          }

          if (blocking_allowed &&
              QpackEntry::Size(name, value) <=
                  header_table_.MaxInsertSizeWithoutEvictingGivenEntry(
                      std::min(smallest_blocking_index, index))) {
            // If allowed, duplicate entry and refer to it.
            encoder_stream_sender_.SendDuplicate(
                QpackAbsoluteIndexToEncoderStreamRelativeIndex(
                    index, header_table_.inserted_entry_count()));
            auto entry = header_table_.InsertEntry(name, value);
            blocking_manager_.OnReferenceSentOnEncoderStream(
                entry->InsertionIndex(), index);
            instructions.push_back(EncodeIndexedHeaderField(
                is_static, entry->InsertionIndex(), referred_indices));
            smallest_blocking_index = std::min(smallest_blocking_index, index);

            break;
          }
        }

        // Encode entry as string literals.
        // TODO(b/112770235): Use already acknowledged entry with lower index if
        // exists.
        // TODO(b/112770235): Use static entry name with literal value if
        // dynamic entry exists but cannot be used.
        instructions.push_back(EncodeLiteralHeaderField(name, value));

        break;

      case QpackHeaderTable::MatchType::kName:
        if (is_static) {
          if (blocking_allowed &&
              QpackEntry::Size(name, value) <=
                  header_table_.MaxInsertSizeWithoutEvictingGivenEntry(
                      smallest_blocking_index)) {
            // If allowed, insert entry into dynamic table and refer to it.
            encoder_stream_sender_.SendInsertWithNameReference(is_static, index,
                                                               value);
            auto entry = header_table_.InsertEntry(name, value);
            instructions.push_back(EncodeIndexedHeaderField(
                /* is_static = */ false, entry->InsertionIndex(),
                referred_indices));
            smallest_blocking_index = std::min<uint64_t>(
                smallest_blocking_index, entry->InsertionIndex());

            break;
          }

          // Emit literal field with name reference.
          instructions.push_back(EncodeLiteralHeaderFieldWithNameReference(
              is_static, index, value, referred_indices));

          break;
        }

        if (blocking_allowed &&
            QpackEntry::Size(name, value) <=
                header_table_.MaxInsertSizeWithoutEvictingGivenEntry(
                    std::min(smallest_blocking_index, index))) {
          // If allowed, insert entry with name reference and refer to it.
          encoder_stream_sender_.SendInsertWithNameReference(
              is_static,
              QpackAbsoluteIndexToEncoderStreamRelativeIndex(
                  index, header_table_.inserted_entry_count()),
              value);
          auto entry = header_table_.InsertEntry(name, value);
          blocking_manager_.OnReferenceSentOnEncoderStream(
              entry->InsertionIndex(), index);
          instructions.push_back(EncodeIndexedHeaderField(
              is_static, entry->InsertionIndex(), referred_indices));
          smallest_blocking_index = std::min(smallest_blocking_index, index);

          break;
        }

        if ((blocking_allowed || index < known_received_count) &&
            index >= draining_index) {
          // If allowed, refer to entry name directly, with literal value.
          instructions.push_back(EncodeLiteralHeaderFieldWithNameReference(
              is_static, index, value, referred_indices));
          smallest_blocking_index = std::min(smallest_blocking_index, index);

          break;
        }

        // Encode entry as string literals.
        // TODO(b/112770235): Use already acknowledged entry with lower index if
        // exists.
        // TODO(b/112770235): Use static entry name with literal value if
        // dynamic entry exists but cannot be used.
        instructions.push_back(EncodeLiteralHeaderField(name, value));

        break;

      case QpackHeaderTable::MatchType::kNoMatch:
        if (blocking_allowed &&
            QpackEntry::Size(name, value) <=
                header_table_.MaxInsertSizeWithoutEvictingGivenEntry(
                    smallest_blocking_index)) {
          // If allowed, insert entry and refer to it.
          encoder_stream_sender_.SendInsertWithoutNameReference(name, value);
          auto entry = header_table_.InsertEntry(name, value);
          instructions.push_back(EncodeIndexedHeaderField(
              /* is_static = */ false, entry->InsertionIndex(),
              referred_indices));
          smallest_blocking_index = std::min<uint64_t>(smallest_blocking_index,
                                                       entry->InsertionIndex());

          break;
        }

        // Encode entry as string literals.
        instructions.push_back(EncodeLiteralHeaderField(name, value));

        break;
    }
  }

  return instructions;
}

std::string QpackEncoder::SecondPassEncode(
    QpackEncoder::Instructions instructions,
    uint64_t required_insert_count) const {
  QpackInstructionEncoder instruction_encoder;
  std::string encoded_headers;

  // Header block prefix.
  QpackInstructionEncoder::Values values;
  values.varint = QpackEncodeRequiredInsertCount(required_insert_count,
                                                 header_table_.max_entries());
  values.varint2 = 0;    // Delta Base.
  values.s_bit = false;  // Delta Base sign.
  const uint64_t base = required_insert_count;

  instruction_encoder.Encode(QpackPrefixInstruction(), values,
                             &encoded_headers);

  for (auto& instruction : instructions) {
    // Dynamic table references must be transformed from absolute to relative
    // indices.
    if ((instruction.instruction == QpackIndexedHeaderFieldInstruction() ||
         instruction.instruction ==
             QpackLiteralHeaderFieldNameReferenceInstruction()) &&
        !instruction.values.s_bit) {
      instruction.values.varint =
          QpackAbsoluteIndexToRequestStreamRelativeIndex(
              instruction.values.varint, base);
    }
    instruction_encoder.Encode(instruction.instruction, instruction.values,
                               &encoded_headers);
  }

  return encoded_headers;
}

std::string QpackEncoder::EncodeHeaderList(
    QuicStreamId stream_id,
    const spdy::SpdyHeaderBlock& header_list) {
  // Keep track of all dynamic table indices that this header block refers to so
  // that it can be passed to QpackBlockingManager.
  QpackBlockingManager::IndexSet referred_indices;

  // First pass: encode into |instructions|.
  Instructions instructions = FirstPassEncode(header_list, &referred_indices);

  const uint64_t required_insert_count =
      referred_indices.empty()
          ? 0
          : QpackBlockingManager::RequiredInsertCount(referred_indices);
  if (!referred_indices.empty()) {
    blocking_manager_.OnHeaderBlockSent(stream_id, std::move(referred_indices));
  }

  // Second pass.
  return SecondPassEncode(std::move(instructions), required_insert_count);
}

void QpackEncoder::SetMaximumDynamicTableCapacity(
    uint64_t maximum_dynamic_table_capacity) {
  header_table_.SetMaximumDynamicTableCapacity(maximum_dynamic_table_capacity);
}

void QpackEncoder::SetDynamicTableCapacity(uint64_t dynamic_table_capacity) {
  encoder_stream_sender_.SendSetDynamicTableCapacity(dynamic_table_capacity);
  bool success = header_table_.SetDynamicTableCapacity(dynamic_table_capacity);
  DCHECK(success);
}

void QpackEncoder::SetMaximumBlockedStreams(uint64_t maximum_blocked_streams) {
  maximum_blocked_streams_ = maximum_blocked_streams;
}

void QpackEncoder::OnInsertCountIncrement(uint64_t increment) {
  if (increment == 0) {
    decoder_stream_error_delegate_->OnDecoderStreamError(
        "Invalid increment value 0.");
    return;
  }

  blocking_manager_.OnInsertCountIncrement(increment);

  if (blocking_manager_.known_received_count() >
      header_table_.inserted_entry_count()) {
    decoder_stream_error_delegate_->OnDecoderStreamError(QuicStrCat(
        "Increment value ", increment, " raises known received count to ",
        blocking_manager_.known_received_count(),
        " exceeding inserted entry count ",
        header_table_.inserted_entry_count()));
  }
}

void QpackEncoder::OnHeaderAcknowledgement(QuicStreamId stream_id) {
  if (!blocking_manager_.OnHeaderAcknowledgement(stream_id)) {
    decoder_stream_error_delegate_->OnDecoderStreamError(
        QuicStrCat("Header Acknowledgement received for stream ", stream_id,
                   " with no outstanding header blocks."));
  }
}

void QpackEncoder::OnStreamCancellation(QuicStreamId stream_id) {
  blocking_manager_.OnStreamCancellation(stream_id);
}

void QpackEncoder::OnErrorDetected(QuicStringPiece error_message) {
  decoder_stream_error_delegate_->OnDecoderStreamError(error_message);
}

}  // namespace quic
