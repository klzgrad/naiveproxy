// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_decoder.h"

#include "base/logging.h"
#include "net/third_party/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"

namespace quic {

QpackDecoder::QpackDecoder(
    EncoderStreamErrorDelegate* encoder_stream_error_delegate,
    QpackDecoderStreamSender::Delegate* decoder_stream_sender_delegate)
    : encoder_stream_error_delegate_(encoder_stream_error_delegate),
      encoder_stream_receiver_(this),
      decoder_stream_sender_(decoder_stream_sender_delegate) {
  DCHECK(encoder_stream_error_delegate_);
  DCHECK(decoder_stream_sender_delegate);
}

QpackDecoder::~QpackDecoder() {}

void QpackDecoder::SetMaximumDynamicTableCapacity(
    uint64_t maximum_dynamic_table_capacity) {
  header_table_.SetMaximumDynamicTableCapacity(maximum_dynamic_table_capacity);
}

void QpackDecoder::OnStreamReset(QuicStreamId stream_id) {
  decoder_stream_sender_.SendStreamCancellation(stream_id);
}

void QpackDecoder::DecodeEncoderStreamData(QuicStringPiece data) {
  encoder_stream_receiver_.Decode(data);
}

void QpackDecoder::OnInsertWithNameReference(bool is_static,
                                             uint64_t name_index,
                                             QuicStringPiece value) {
  if (is_static) {
    auto entry = header_table_.LookupEntry(/* is_static = */ true, name_index);
    if (!entry) {
      encoder_stream_error_delegate_->OnError("Invalid static table entry.");
      return;
    }

    entry = header_table_.InsertEntry(entry->name(), value);
    if (!entry) {
      encoder_stream_error_delegate_->OnError(
          "Error inserting entry with name reference.");
    }
    return;
  }

  uint64_t real_index;
  if (!EncoderStreamRelativeIndexToRealIndex(name_index, &real_index)) {
    encoder_stream_error_delegate_->OnError("Invalid relative index.");
    return;
  }

  const QpackEntry* entry =
      header_table_.LookupEntry(/* is_static = */ false, real_index);
  if (!entry) {
    encoder_stream_error_delegate_->OnError("Dynamic table entry not found.");
    return;
  }
  entry = header_table_.InsertEntry(entry->name(), value);
  if (!entry) {
    encoder_stream_error_delegate_->OnError(
        "Error inserting entry with name reference.");
  }
}

void QpackDecoder::OnInsertWithoutNameReference(QuicStringPiece name,
                                                QuicStringPiece value) {
  const QpackEntry* entry = header_table_.InsertEntry(name, value);
  if (!entry) {
    encoder_stream_error_delegate_->OnError("Error inserting literal entry.");
  }
}

void QpackDecoder::OnDuplicate(uint64_t index) {
  uint64_t real_index;
  if (!EncoderStreamRelativeIndexToRealIndex(index, &real_index)) {
    encoder_stream_error_delegate_->OnError("Invalid relative index.");
    return;
  }

  const QpackEntry* entry =
      header_table_.LookupEntry(/* is_static = */ false, real_index);
  if (!entry) {
    encoder_stream_error_delegate_->OnError("Dynamic table entry not found.");
    return;
  }
  entry = header_table_.InsertEntry(entry->name(), entry->value());
  if (!entry) {
    encoder_stream_error_delegate_->OnError("Error inserting duplicate entry.");
  }
}

void QpackDecoder::OnDynamicTableSizeUpdate(uint64_t max_size) {
  if (!header_table_.UpdateTableSize(max_size)) {
    encoder_stream_error_delegate_->OnError(
        "Error updating dynamic table size.");
  }
}

void QpackDecoder::OnErrorDetected(QuicStringPiece error_message) {
  encoder_stream_error_delegate_->OnError(error_message);
}

bool QpackDecoder::EncoderStreamRelativeIndexToRealIndex(
    uint64_t relative_index,
    uint64_t* real_index) const {
  if (relative_index == std::numeric_limits<uint64_t>::max() ||
      relative_index + 1 > std::numeric_limits<uint64_t>::max() -
                               header_table_.inserted_entry_count()) {
    return false;
  }

  *real_index = header_table_.inserted_entry_count() - relative_index - 1;
  return true;
}

std::unique_ptr<QpackProgressiveDecoder> QpackDecoder::DecodeHeaderBlock(
    QuicStreamId stream_id,
    QpackProgressiveDecoder::HeadersHandlerInterface* handler) {
  return QuicMakeUnique<QpackProgressiveDecoder>(
      stream_id, &header_table_, &decoder_stream_sender_, handler);
}

}  // namespace quic
