// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/qpack_decoder.h"

#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/qpack/qpack_index_conversions.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

QpackDecoder::QpackDecoder(
    uint64_t maximum_dynamic_table_capacity, uint64_t maximum_blocked_streams,
    EncoderStreamErrorDelegate* encoder_stream_error_delegate)
    : encoder_stream_error_delegate_(encoder_stream_error_delegate),
      encoder_stream_receiver_(this),
      maximum_blocked_streams_(maximum_blocked_streams),
      known_received_count_(0) {
  QUICHE_DCHECK(encoder_stream_error_delegate_);

  header_table_.SetMaximumDynamicTableCapacity(maximum_dynamic_table_capacity);
}

QpackDecoder::~QpackDecoder() {}

void QpackDecoder::OnStreamReset(QuicStreamId stream_id) {
  if (header_table_.maximum_dynamic_table_capacity() > 0) {
    decoder_stream_sender_.SendStreamCancellation(stream_id);
    if (!GetQuicRestartFlag(quic_opport_bundle_qpack_decoder_data3)) {
      decoder_stream_sender_.Flush();
    }
  }
}

bool QpackDecoder::OnStreamBlocked(QuicStreamId stream_id) {
  auto result = blocked_streams_.insert(stream_id);
  QUICHE_DCHECK(result.second);
  return blocked_streams_.size() <= maximum_blocked_streams_;
}

void QpackDecoder::OnStreamUnblocked(QuicStreamId stream_id) {
  size_t result = blocked_streams_.erase(stream_id);
  QUICHE_DCHECK_EQ(1u, result);
}

void QpackDecoder::OnDecodingCompleted(QuicStreamId stream_id,
                                       uint64_t required_insert_count) {
  if (required_insert_count > 0) {
    decoder_stream_sender_.SendHeaderAcknowledgement(stream_id);

    if (known_received_count_ < required_insert_count) {
      known_received_count_ = required_insert_count;
    }
  }

  // Send an Insert Count Increment instruction if not all dynamic table entries
  // have been acknowledged yet.  This is necessary for efficient compression in
  // case the encoder chooses not to reference unacknowledged dynamic table
  // entries, otherwise inserted entries would never be acknowledged.
  if (known_received_count_ < header_table_.inserted_entry_count()) {
    decoder_stream_sender_.SendInsertCountIncrement(
        header_table_.inserted_entry_count() - known_received_count_);
    known_received_count_ = header_table_.inserted_entry_count();
  }

  if (!GetQuicRestartFlag(quic_opport_bundle_qpack_decoder_data3)) {
    decoder_stream_sender_.Flush();
  }
}

void QpackDecoder::OnInsertWithNameReference(bool is_static,
                                             uint64_t name_index,
                                             absl::string_view value) {
  if (is_static) {
    auto entry = header_table_.LookupEntry(/* is_static = */ true, name_index);
    if (!entry) {
      OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_INVALID_STATIC_ENTRY,
                      "Invalid static table entry.");
      return;
    }

    if (!header_table_.EntryFitsDynamicTableCapacity(entry->name(), value)) {
      OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_ERROR_INSERTING_STATIC,
                      "Error inserting entry with name reference.");
      return;
    }
    header_table_.InsertEntry(entry->name(), value);
    return;
  }

  uint64_t absolute_index;
  if (!QpackEncoderStreamRelativeIndexToAbsoluteIndex(
          name_index, header_table_.inserted_entry_count(), &absolute_index)) {
    OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_INSERTION_INVALID_RELATIVE_INDEX,
                    "Invalid relative index.");
    return;
  }

  const QpackEntry* entry =
      header_table_.LookupEntry(/* is_static = */ false, absolute_index);
  if (!entry) {
    OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_INSERTION_DYNAMIC_ENTRY_NOT_FOUND,
                    "Dynamic table entry not found.");
    return;
  }
  if (!header_table_.EntryFitsDynamicTableCapacity(entry->name(), value)) {
    OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_ERROR_INSERTING_DYNAMIC,
                    "Error inserting entry with name reference.");
    return;
  }
  header_table_.InsertEntry(entry->name(), value);
}

void QpackDecoder::OnInsertWithoutNameReference(absl::string_view name,
                                                absl::string_view value) {
  if (!header_table_.EntryFitsDynamicTableCapacity(name, value)) {
    OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_ERROR_INSERTING_LITERAL,
                    "Error inserting literal entry.");
    return;
  }
  header_table_.InsertEntry(name, value);
}

void QpackDecoder::OnDuplicate(uint64_t index) {
  uint64_t absolute_index;
  if (!QpackEncoderStreamRelativeIndexToAbsoluteIndex(
          index, header_table_.inserted_entry_count(), &absolute_index)) {
    OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_DUPLICATE_INVALID_RELATIVE_INDEX,
                    "Invalid relative index.");
    return;
  }

  const QpackEntry* entry =
      header_table_.LookupEntry(/* is_static = */ false, absolute_index);
  if (!entry) {
    OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_DUPLICATE_DYNAMIC_ENTRY_NOT_FOUND,
                    "Dynamic table entry not found.");
    return;
  }
  if (!header_table_.EntryFitsDynamicTableCapacity(entry->name(),
                                                   entry->value())) {
    // This is impossible since entry was retrieved from the dynamic table.
    OnErrorDetected(QUIC_INTERNAL_ERROR, "Error inserting duplicate entry.");
    return;
  }
  header_table_.InsertEntry(entry->name(), entry->value());
}

void QpackDecoder::OnSetDynamicTableCapacity(uint64_t capacity) {
  if (!header_table_.SetDynamicTableCapacity(capacity)) {
    OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_SET_DYNAMIC_TABLE_CAPACITY,
                    "Error updating dynamic table capacity.");
  }
}

void QpackDecoder::OnErrorDetected(QuicErrorCode error_code,
                                   absl::string_view error_message) {
  encoder_stream_error_delegate_->OnEncoderStreamError(error_code,
                                                       error_message);
}

std::unique_ptr<QpackProgressiveDecoder> QpackDecoder::CreateProgressiveDecoder(
    QuicStreamId stream_id,
    QpackProgressiveDecoder::HeadersHandlerInterface* handler) {
  return std::make_unique<QpackProgressiveDecoder>(stream_id, this, this,
                                                   &header_table_, handler);
}

void QpackDecoder::FlushDecoderStream() { decoder_stream_sender_.Flush(); }

}  // namespace quic
