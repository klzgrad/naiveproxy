// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/qpack_encoder.h"

#include <algorithm>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/qpack/qpack_index_conversions.h"
#include "quiche/quic/core/qpack/qpack_instruction_encoder.h"
#include "quiche/quic/core/qpack/qpack_required_insert_count.h"
#include "quiche/quic/core/qpack/value_splitting_header_list.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"

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
      maximum_blocked_streams_(0),
      header_list_count_(0) {
  QUICHE_DCHECK(decoder_stream_error_delegate_);
}

QpackEncoder::~QpackEncoder() {}

// static
QpackEncoder::Representation QpackEncoder::EncodeIndexedHeaderField(
    bool is_static, uint64_t index,
    QpackBlockingManager::IndexSet* referred_indices) {
  // Add |index| to |*referred_indices| only if entry is in the dynamic table.
  if (!is_static) {
    referred_indices->insert(index);
  }
  return Representation::IndexedHeaderField(is_static, index);
}

// static
QpackEncoder::Representation
QpackEncoder::EncodeLiteralHeaderFieldWithNameReference(
    bool is_static, uint64_t index, absl::string_view value,
    QpackBlockingManager::IndexSet* referred_indices) {
  // Add |index| to |*referred_indices| only if entry is in the dynamic table.
  if (!is_static) {
    referred_indices->insert(index);
  }
  return Representation::LiteralHeaderFieldNameReference(is_static, index,
                                                         value);
}

// static
QpackEncoder::Representation QpackEncoder::EncodeLiteralHeaderField(
    absl::string_view name, absl::string_view value) {
  return Representation::LiteralHeaderField(name, value);
}

QpackEncoder::Representations QpackEncoder::FirstPassEncode(
    QuicStreamId stream_id, const spdy::Http2HeaderBlock& header_list,
    QpackBlockingManager::IndexSet* referred_indices,
    QuicByteCount* encoder_stream_sent_byte_count) {
  // If previous instructions are buffered in |encoder_stream_sender_|,
  // do not count them towards the current header block.
  const QuicByteCount initial_encoder_stream_buffered_byte_count =
      encoder_stream_sender_.BufferedByteCount();

  const bool can_write_to_encoder_stream = encoder_stream_sender_.CanWrite();

  Representations representations;
  representations.reserve(header_list.size());

  // Entries with index larger than or equal to |known_received_count| are
  // blocking.
  const uint64_t known_received_count =
      blocking_manager_.known_received_count();

  // The index of the oldest entry that must not be evicted. Blocking entries
  // must not be evicted. Also, unacknowledged entries must not be evicted,
  // even if they have no outstanding references (see https://crbug.com/1441880
  // for more context).
  uint64_t smallest_non_evictable_index = std::min(
      blocking_manager_.smallest_blocking_index(), known_received_count);

  // Only entries with index greater than or equal to |draining_index| are
  // allowed to be referenced.
  const uint64_t draining_index =
      header_table_.draining_index(kDrainingFraction);
  // Blocking references are allowed if the number of blocked streams is less
  // than the limit.
  const bool blocking_allowed = blocking_manager_.blocking_allowed_on_stream(
      stream_id, maximum_blocked_streams_);

  // Track events for histograms.
  bool dynamic_table_insertion_blocked = false;
  bool blocked_stream_limit_exhausted = false;

  for (const auto& header : ValueSplittingHeaderList(&header_list)) {
    // These strings are owned by |header_list|.
    absl::string_view name = header.first;
    absl::string_view value = header.second;

    bool is_static;
    uint64_t index;

    auto match_type =
        header_table_.FindHeaderField(name, value, &is_static, &index);

    switch (match_type) {
      case QpackEncoderHeaderTable::MatchType::kNameAndValue:
        if (is_static) {
          // Refer to entry directly.
          representations.push_back(
              EncodeIndexedHeaderField(is_static, index, referred_indices));

          break;
        }

        if (index >= draining_index) {
          // If allowed, refer to entry directly.
          if (!blocking_allowed && index >= known_received_count) {
            blocked_stream_limit_exhausted = true;
          } else {
            representations.push_back(
                EncodeIndexedHeaderField(is_static, index, referred_indices));
            smallest_non_evictable_index =
                std::min(smallest_non_evictable_index, index);
            header_table_.set_dynamic_table_entry_referenced();

            break;
          }
        } else {
          // Entry is draining, needs to be duplicated.
          if (!blocking_allowed) {
            blocked_stream_limit_exhausted = true;
          } else if (QpackEntry::Size(name, value) >
                     header_table_.MaxInsertSizeWithoutEvictingGivenEntry(
                         std::min(smallest_non_evictable_index, index))) {
            dynamic_table_insertion_blocked = true;
          } else {
            if (can_write_to_encoder_stream) {
              // If allowed, duplicate entry and refer to it.
              encoder_stream_sender_.SendDuplicate(
                  QpackAbsoluteIndexToEncoderStreamRelativeIndex(
                      index, header_table_.inserted_entry_count()));
              uint64_t new_index = header_table_.InsertEntry(name, value);
              representations.push_back(EncodeIndexedHeaderField(
                  is_static, new_index, referred_indices));
              smallest_non_evictable_index =
                  std::min(smallest_non_evictable_index, index);
              header_table_.set_dynamic_table_entry_referenced();

              break;
            }
          }
        }

        // Encode entry as string literals.
        // TODO(b/112770235): Use already acknowledged entry with lower index if
        // exists.
        // TODO(b/112770235): Use static entry name with literal value if
        // dynamic entry exists but cannot be used.
        representations.push_back(EncodeLiteralHeaderField(name, value));

        break;

      case QpackEncoderHeaderTable::MatchType::kName:
        if (is_static) {
          if (blocking_allowed &&
              QpackEntry::Size(name, value) <=
                  header_table_.MaxInsertSizeWithoutEvictingGivenEntry(
                      smallest_non_evictable_index)) {
            // If allowed, insert entry into dynamic table and refer to it.
            if (can_write_to_encoder_stream) {
              encoder_stream_sender_.SendInsertWithNameReference(is_static,
                                                                 index, value);
              uint64_t new_index = header_table_.InsertEntry(name, value);
              representations.push_back(EncodeIndexedHeaderField(
                  /* is_static = */ false, new_index, referred_indices));
              smallest_non_evictable_index =
                  std::min<uint64_t>(smallest_non_evictable_index, new_index);

              break;
            }
          }

          // Emit literal field with name reference.
          representations.push_back(EncodeLiteralHeaderFieldWithNameReference(
              is_static, index, value, referred_indices));

          break;
        }

        if (!blocking_allowed) {
          blocked_stream_limit_exhausted = true;
        } else if (QpackEntry::Size(name, value) >
                   header_table_.MaxInsertSizeWithoutEvictingGivenEntry(
                       std::min(smallest_non_evictable_index, index))) {
          dynamic_table_insertion_blocked = true;
        } else {
          // If allowed, insert entry with name reference and refer to it.
          if (can_write_to_encoder_stream) {
            encoder_stream_sender_.SendInsertWithNameReference(
                is_static,
                QpackAbsoluteIndexToEncoderStreamRelativeIndex(
                    index, header_table_.inserted_entry_count()),
                value);
            uint64_t new_index = header_table_.InsertEntry(name, value);
            representations.push_back(EncodeIndexedHeaderField(
                is_static, new_index, referred_indices));
            smallest_non_evictable_index =
                std::min(smallest_non_evictable_index, index);
            header_table_.set_dynamic_table_entry_referenced();

            break;
          }
        }

        if ((blocking_allowed || index < known_received_count) &&
            index >= draining_index) {
          // If allowed, refer to entry name directly, with literal value.
          representations.push_back(EncodeLiteralHeaderFieldWithNameReference(
              is_static, index, value, referred_indices));
          smallest_non_evictable_index =
              std::min(smallest_non_evictable_index, index);
          header_table_.set_dynamic_table_entry_referenced();

          break;
        }

        // Encode entry as string literals.
        // TODO(b/112770235): Use already acknowledged entry with lower index if
        // exists.
        // TODO(b/112770235): Use static entry name with literal value if
        // dynamic entry exists but cannot be used.
        representations.push_back(EncodeLiteralHeaderField(name, value));

        break;

      case QpackEncoderHeaderTable::MatchType::kNoMatch:
        // If allowed, insert entry and refer to it.
        if (!blocking_allowed) {
          blocked_stream_limit_exhausted = true;
        } else if (QpackEntry::Size(name, value) >
                   header_table_.MaxInsertSizeWithoutEvictingGivenEntry(
                       smallest_non_evictable_index)) {
          dynamic_table_insertion_blocked = true;
        } else {
          if (can_write_to_encoder_stream) {
            encoder_stream_sender_.SendInsertWithoutNameReference(name, value);
            uint64_t new_index = header_table_.InsertEntry(name, value);
            representations.push_back(EncodeIndexedHeaderField(
                /* is_static = */ false, new_index, referred_indices));
            smallest_non_evictable_index =
                std::min<uint64_t>(smallest_non_evictable_index, new_index);

            break;
          }
        }

        // Encode entry as string literals.
        // TODO(b/112770235): Consider also adding to dynamic table to improve
        // compression ratio for subsequent header blocks with peers that do not
        // allow any blocked streams.
        representations.push_back(EncodeLiteralHeaderField(name, value));

        break;
    }
  }

  const QuicByteCount encoder_stream_buffered_byte_count =
      encoder_stream_sender_.BufferedByteCount();
  QUICHE_DCHECK_GE(encoder_stream_buffered_byte_count,
                   initial_encoder_stream_buffered_byte_count);

  if (encoder_stream_sent_byte_count) {
    *encoder_stream_sent_byte_count =
        encoder_stream_buffered_byte_count -
        initial_encoder_stream_buffered_byte_count;
  }
  if (can_write_to_encoder_stream) {
    encoder_stream_sender_.Flush();
  } else {
    QUICHE_DCHECK_EQ(encoder_stream_buffered_byte_count,
                     initial_encoder_stream_buffered_byte_count);
  }

  ++header_list_count_;

  if (dynamic_table_insertion_blocked) {
    QUIC_HISTOGRAM_COUNTS(
        "QuicSession.Qpack.HeaderListCountWhenInsertionBlocked",
        header_list_count_, /* min = */ 1, /* max = */ 1000,
        /* bucket_count = */ 50,
        "The ordinality of a header list within a connection during the "
        "encoding of which at least one dynamic table insertion was "
        "blocked.");
  } else {
    QUIC_HISTOGRAM_COUNTS(
        "QuicSession.Qpack.HeaderListCountWhenInsertionNotBlocked",
        header_list_count_, /* min = */ 1, /* max = */ 1000,
        /* bucket_count = */ 50,
        "The ordinality of a header list within a connection during the "
        "encoding of which no dynamic table insertion was blocked.");
  }

  if (blocked_stream_limit_exhausted) {
    QUIC_HISTOGRAM_COUNTS(
        "QuicSession.Qpack.HeaderListCountWhenBlockedStreamLimited",
        header_list_count_, /* min = */ 1, /* max = */ 1000,
        /* bucket_count = */ 50,
        "The ordinality of a header list within a connection during the "
        "encoding of which unacknowledged dynamic table entries could not be "
        "referenced due to the limit on the number of blocked streams.");
  } else {
    QUIC_HISTOGRAM_COUNTS(
        "QuicSession.Qpack.HeaderListCountWhenNotBlockedStreamLimited",
        header_list_count_, /* min = */ 1, /* max = */ 1000,
        /* bucket_count = */ 50,
        "The ordinality of a header list within a connection during the "
        "encoding of which the limit on the number of blocked streams did "
        "not "
        "prevent referencing unacknowledged dynamic table entries.");
  }

  return representations;
}

std::string QpackEncoder::SecondPassEncode(
    QpackEncoder::Representations representations,
    uint64_t required_insert_count) const {
  QpackInstructionEncoder instruction_encoder;
  std::string encoded_headers;

  // Header block prefix.
  instruction_encoder.Encode(
      Representation::Prefix(QpackEncodeRequiredInsertCount(
          required_insert_count, header_table_.max_entries())),
      &encoded_headers);

  const uint64_t base = required_insert_count;

  for (auto& representation : representations) {
    // Dynamic table references must be transformed from absolute to relative
    // indices.
    if ((representation.instruction() == QpackIndexedHeaderFieldInstruction() ||
         representation.instruction() ==
             QpackLiteralHeaderFieldNameReferenceInstruction()) &&
        !representation.s_bit()) {
      representation.set_varint(QpackAbsoluteIndexToRequestStreamRelativeIndex(
          representation.varint(), base));
    }
    instruction_encoder.Encode(representation, &encoded_headers);
  }

  return encoded_headers;
}

std::string QpackEncoder::EncodeHeaderList(
    QuicStreamId stream_id, const spdy::Http2HeaderBlock& header_list,
    QuicByteCount* encoder_stream_sent_byte_count) {
  // Keep track of all dynamic table indices that this header block refers to so
  // that it can be passed to QpackBlockingManager.
  QpackBlockingManager::IndexSet referred_indices;

  // First pass: encode into |representations|.
  Representations representations =
      FirstPassEncode(stream_id, header_list, &referred_indices,
                      encoder_stream_sent_byte_count);

  const uint64_t required_insert_count =
      referred_indices.empty()
          ? 0
          : QpackBlockingManager::RequiredInsertCount(referred_indices);
  if (!referred_indices.empty()) {
    blocking_manager_.OnHeaderBlockSent(stream_id, std::move(referred_indices));
  }

  // Second pass.
  return SecondPassEncode(std::move(representations), required_insert_count);
}

bool QpackEncoder::SetMaximumDynamicTableCapacity(
    uint64_t maximum_dynamic_table_capacity) {
  return header_table_.SetMaximumDynamicTableCapacity(
      maximum_dynamic_table_capacity);
}

void QpackEncoder::SetDynamicTableCapacity(uint64_t dynamic_table_capacity) {
  encoder_stream_sender_.SendSetDynamicTableCapacity(dynamic_table_capacity);
  // Do not flush encoder stream.  This write can safely be delayed until more
  // instructions are written.

  bool success = header_table_.SetDynamicTableCapacity(dynamic_table_capacity);
  QUICHE_DCHECK(success);
}

bool QpackEncoder::SetMaximumBlockedStreams(uint64_t maximum_blocked_streams) {
  if (maximum_blocked_streams < maximum_blocked_streams_) {
    return false;
  }
  maximum_blocked_streams_ = maximum_blocked_streams;
  return true;
}

void QpackEncoder::OnInsertCountIncrement(uint64_t increment) {
  if (increment == 0) {
    OnErrorDetected(QUIC_QPACK_DECODER_STREAM_INVALID_ZERO_INCREMENT,
                    "Invalid increment value 0.");
    return;
  }

  if (!blocking_manager_.OnInsertCountIncrement(increment)) {
    OnErrorDetected(QUIC_QPACK_DECODER_STREAM_INCREMENT_OVERFLOW,
                    "Insert Count Increment instruction causes overflow.");
  }

  if (blocking_manager_.known_received_count() >
      header_table_.inserted_entry_count()) {
    OnErrorDetected(QUIC_QPACK_DECODER_STREAM_IMPOSSIBLE_INSERT_COUNT,
                    absl::StrCat("Increment value ", increment,
                                 " raises known received count to ",
                                 blocking_manager_.known_received_count(),
                                 " exceeding inserted entry count ",
                                 header_table_.inserted_entry_count()));
  }
}

void QpackEncoder::OnHeaderAcknowledgement(QuicStreamId stream_id) {
  if (!blocking_manager_.OnHeaderAcknowledgement(stream_id)) {
    OnErrorDetected(
        QUIC_QPACK_DECODER_STREAM_INCORRECT_ACKNOWLEDGEMENT,
        absl::StrCat("Header Acknowledgement received for stream ", stream_id,
                     " with no outstanding header blocks."));
  }
}

void QpackEncoder::OnStreamCancellation(QuicStreamId stream_id) {
  blocking_manager_.OnStreamCancellation(stream_id);
}

void QpackEncoder::OnErrorDetected(QuicErrorCode error_code,
                                   absl::string_view error_message) {
  decoder_stream_error_delegate_->OnDecoderStreamError(error_code,
                                                       error_message);
}

}  // namespace quic
