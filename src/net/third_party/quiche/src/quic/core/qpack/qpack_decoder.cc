// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_index_conversions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QpackDecoder::QpackDecoder(
    uint64_t maximum_dynamic_table_capacity,
    uint64_t maximum_blocked_streams,
    EncoderStreamErrorDelegate* encoder_stream_error_delegate)
    : encoder_stream_error_delegate_(encoder_stream_error_delegate),
      encoder_stream_receiver_(this),
      maximum_blocked_streams_(maximum_blocked_streams),
      known_received_count_(0) {
  DCHECK(encoder_stream_error_delegate_);

  header_table_.SetMaximumDynamicTableCapacity(maximum_dynamic_table_capacity);
}

QpackDecoder::~QpackDecoder() {}

void QpackDecoder::OnStreamReset(QuicStreamId stream_id) {
  if (header_table_.maximum_dynamic_table_capacity() > 0) {
    decoder_stream_sender_.SendStreamCancellation(stream_id);
    decoder_stream_sender_.Flush();
  }
}

bool QpackDecoder::OnStreamBlocked(QuicStreamId stream_id) {
  auto result = blocked_streams_.insert(stream_id);
  DCHECK(result.second);
  return blocked_streams_.size() <= maximum_blocked_streams_;
}

void QpackDecoder::OnStreamUnblocked(QuicStreamId stream_id) {
  size_t result = blocked_streams_.erase(stream_id);
  DCHECK_EQ(1u, result);
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

  decoder_stream_sender_.Flush();
}

void QpackDecoder::OnInsertWithNameReference(bool is_static,
                                             uint64_t name_index,
                                             quiche::QuicheStringPiece value) {
  if (is_static) {
    auto entry = header_table_.LookupEntry(/* is_static = */ true, name_index);
    if (!entry) {
      encoder_stream_error_delegate_->OnEncoderStreamError(
          "Invalid static table entry.");
      return;
    }

    entry = header_table_.InsertEntry(entry->name(), value);
    if (!entry) {
      encoder_stream_error_delegate_->OnEncoderStreamError(
          "Error inserting entry with name reference.");
    }
    return;
  }

  uint64_t absolute_index;
  if (!QpackEncoderStreamRelativeIndexToAbsoluteIndex(
          name_index, header_table_.inserted_entry_count(), &absolute_index)) {
    encoder_stream_error_delegate_->OnEncoderStreamError(
        "Invalid relative index.");
    return;
  }

  const QpackEntry* entry =
      header_table_.LookupEntry(/* is_static = */ false, absolute_index);
  if (!entry) {
    encoder_stream_error_delegate_->OnEncoderStreamError(
        "Dynamic table entry not found.");
    return;
  }
  entry = header_table_.InsertEntry(entry->name(), value);
  if (!entry) {
    encoder_stream_error_delegate_->OnEncoderStreamError(
        "Error inserting entry with name reference.");
  }
}

void QpackDecoder::OnInsertWithoutNameReference(
    quiche::QuicheStringPiece name,
    quiche::QuicheStringPiece value) {
  const QpackEntry* entry = header_table_.InsertEntry(name, value);
  if (!entry) {
    encoder_stream_error_delegate_->OnEncoderStreamError(
        "Error inserting literal entry.");
  }
}

void QpackDecoder::OnDuplicate(uint64_t index) {
  uint64_t absolute_index;
  if (!QpackEncoderStreamRelativeIndexToAbsoluteIndex(
          index, header_table_.inserted_entry_count(), &absolute_index)) {
    encoder_stream_error_delegate_->OnEncoderStreamError(
        "Invalid relative index.");
    return;
  }

  const QpackEntry* entry =
      header_table_.LookupEntry(/* is_static = */ false, absolute_index);
  if (!entry) {
    encoder_stream_error_delegate_->OnEncoderStreamError(
        "Dynamic table entry not found.");
    return;
  }
  entry = header_table_.InsertEntry(entry->name(), entry->value());
  if (!entry) {
    encoder_stream_error_delegate_->OnEncoderStreamError(
        "Error inserting duplicate entry.");
  }
}

void QpackDecoder::OnSetDynamicTableCapacity(uint64_t capacity) {
  if (!header_table_.SetDynamicTableCapacity(capacity)) {
    encoder_stream_error_delegate_->OnEncoderStreamError(
        "Error updating dynamic table capacity.");
  }
}

void QpackDecoder::OnErrorDetected(quiche::QuicheStringPiece error_message) {
  encoder_stream_error_delegate_->OnEncoderStreamError(error_message);
}

std::unique_ptr<QpackProgressiveDecoder> QpackDecoder::CreateProgressiveDecoder(
    QuicStreamId stream_id,
    QpackProgressiveDecoder::HeadersHandlerInterface* handler) {
  return std::make_unique<QpackProgressiveDecoder>(stream_id, this, this,
                                                   &header_table_, handler);
}

}  // namespace quic
