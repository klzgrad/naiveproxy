// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder.h"

#include "net/third_party/quiche/src/quic/core/qpack/qpack_index_conversions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"

namespace quic {

QpackDecoder::QpackDecoder(
    uint64_t maximum_dynamic_table_capacity,
    uint64_t maximum_blocked_streams,
    EncoderStreamErrorDelegate* encoder_stream_error_delegate)
    : encoder_stream_error_delegate_(encoder_stream_error_delegate),
      encoder_stream_receiver_(this),
      maximum_blocked_streams_(maximum_blocked_streams) {
  DCHECK(encoder_stream_error_delegate_);

  header_table_.SetMaximumDynamicTableCapacity(maximum_dynamic_table_capacity);
}

QpackDecoder::~QpackDecoder() {}

void QpackDecoder::OnStreamReset(QuicStreamId stream_id) {
  // TODO(bnc): SendStreamCancellation should not be called if maximum dynamic
  // table capacity is zero.
  decoder_stream_sender_.SendStreamCancellation(stream_id);
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

void QpackDecoder::OnInsertWithNameReference(bool is_static,
                                             uint64_t name_index,
                                             QuicStringPiece value) {
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

void QpackDecoder::OnInsertWithoutNameReference(QuicStringPiece name,
                                                QuicStringPiece value) {
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

void QpackDecoder::OnErrorDetected(QuicStringPiece error_message) {
  encoder_stream_error_delegate_->OnEncoderStreamError(error_message);
}

std::unique_ptr<QpackProgressiveDecoder> QpackDecoder::CreateProgressiveDecoder(
    QuicStreamId stream_id,
    QpackProgressiveDecoder::HeadersHandlerInterface* handler) {
  return QuicMakeUnique<QpackProgressiveDecoder>(
      stream_id, this, &header_table_, &decoder_stream_sender_, handler);
}

}  // namespace quic
