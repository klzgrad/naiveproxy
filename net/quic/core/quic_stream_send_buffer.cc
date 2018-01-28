// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/quic_data_writer.h"
#include "net/quic/core/quic_stream_send_buffer.h"
#include "net/quic/core/quic_utils.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"

namespace net {

BufferedSlice::BufferedSlice(QuicMemSlice mem_slice, QuicStreamOffset offset)
    : slice(std::move(mem_slice)),
      offset(offset),
      outstanding_data_length(slice.length()) {}

BufferedSlice::BufferedSlice(BufferedSlice&& other) = default;

BufferedSlice& BufferedSlice::operator=(BufferedSlice&& other) = default;

BufferedSlice::~BufferedSlice() {}

QuicStreamSendBuffer::QuicStreamSendBuffer(QuicBufferAllocator* allocator)
    : stream_offset_(0), allocator_(allocator) {}

QuicStreamSendBuffer::~QuicStreamSendBuffer() {}

void QuicStreamSendBuffer::SaveStreamData(QuicIOVector iov,
                                          size_t iov_offset,
                                          QuicByteCount data_length) {
  DCHECK_LT(0u, data_length);
  // Latch the maximum data slice size.
  const QuicByteCount max_data_slice_size =
      GetQuicFlag(FLAGS_quic_send_buffer_max_data_slice_size);
  while (data_length > 0) {
    size_t slice_len = std::min(data_length, max_data_slice_size);
    QuicMemSlice slice(allocator_, slice_len);
    QuicUtils::CopyToBuffer(iov, iov_offset, slice_len,
                            const_cast<char*>(slice.data()));
    SaveMemSlice(std::move(slice));
    data_length -= slice_len;
    iov_offset += slice_len;
  }
}

void QuicStreamSendBuffer::SaveMemSlice(QuicMemSlice slice) {
  if (slice.empty()) {
    QUIC_BUG << "Try to save empty MemSlice to send buffer.";
    return;
  }
  size_t length = slice.length();
  buffered_slices_.emplace_back(std::move(slice), stream_offset_);
  stream_offset_ += length;
}

bool QuicStreamSendBuffer::WriteStreamData(QuicStreamOffset offset,
                                           QuicByteCount data_length,
                                           QuicDataWriter* writer) {
  for (const BufferedSlice& slice : buffered_slices_) {
    if (offset < slice.offset) {
      break;
    }
    if (offset >= slice.offset + slice.slice.length()) {
      continue;
    }
    QuicByteCount slice_offset = offset - slice.offset;
    QuicByteCount copy_length =
        std::min(data_length, slice.slice.length() - slice_offset);
    if (!writer->WriteBytes(slice.slice.data() + slice_offset, copy_length)) {
      return false;
    }
    offset += copy_length;
    data_length -= copy_length;
  }

  return data_length == 0;
}

void QuicStreamSendBuffer::RemoveStreamFrame(QuicStreamOffset offset,
                                             QuicByteCount data_length) {
  for (BufferedSlice& slice : buffered_slices_) {
    if (offset < slice.offset) {
      break;
    }
    if (offset >= slice.offset + slice.slice.length()) {
      continue;
    }
    QuicByteCount slice_offset = offset - slice.offset;
    QuicByteCount removing_length =
        std::min(data_length, slice.slice.length() - slice_offset);
    slice.outstanding_data_length -= removing_length;
    offset += removing_length;
    data_length -= removing_length;
  }
  DCHECK_EQ(0u, data_length);

  // Remove data which stops waiting for acks. Please note, data can be
  // acked out of order, but send buffer is cleaned up in order.
  while (!buffered_slices_.empty() &&
         buffered_slices_.front().outstanding_data_length == 0) {
    buffered_slices_.pop_front();
  }
}

size_t QuicStreamSendBuffer::size() const {
  return buffered_slices_.size();
}

}  // namespace net
