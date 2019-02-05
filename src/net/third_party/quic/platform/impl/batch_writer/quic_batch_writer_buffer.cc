// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/batch_writer/quic_batch_writer_buffer.h"

#include <sstream>

namespace quic {

QuicBatchWriterBuffer::QuicBatchWriterBuffer() {
  memset(buffer_, 0, sizeof(buffer_));
}

QuicString QuicBatchWriterBuffer::DebugString() const {
  std::ostringstream os;
  os << "{ buffer: " << static_cast<const void*>(buffer_)
     << " buffer_end: " << static_cast<const void*>(buffer_end())
     << " buffered_writes_.size(): " << buffered_writes_.size()
     << " next_write_loc: " << static_cast<const void*>(GetNextWriteLocation())
     << " SizeInUse: " << SizeInUse() << " }";
  return os.str();
}

bool QuicBatchWriterBuffer::Invariants() const {
  // Buffers in buffered_writes_ should not overlap, and collectively they
  // should cover a continuous prefix of buffer_.
  const char* next_buffer = buffer_;
  for (auto iter = buffered_writes_.begin(); iter != buffered_writes_.end();
       ++iter) {
    if ((iter->buffer != next_buffer) ||
        (iter->buffer + iter->buf_len > buffer_end())) {
      return false;
    }
    next_buffer += iter->buf_len;
  }

  return (next_buffer - buffer_) == static_cast<int>(SizeInUse());
}

char* QuicBatchWriterBuffer::GetNextWriteLocation() const {
  const char* next_loc =
      buffered_writes_.empty()
          ? buffer_
          : buffered_writes_.back().buffer + buffered_writes_.back().buf_len;
  if (buffer_end() - next_loc < static_cast<int>(kMaxPacketSize)) {
    return nullptr;
  }
  return const_cast<char*>(next_loc);
}

QuicBatchWriterBuffer::PushResult QuicBatchWriterBuffer::PushBufferedWrite(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    const PerPacketOptions* options) {
  DCHECK(Invariants());
  DCHECK_LE(buf_len, kMaxPacketSize);

  PushResult result = {/*succeeded=*/false, /*buffer_copied=*/false};
  char* next_write_location = GetNextWriteLocation();
  if (next_write_location == nullptr) {
    return result;
  }

  if (buffer != next_write_location) {
    if (IsExternalBuffer(buffer, buf_len)) {
      memcpy(next_write_location, buffer, buf_len);
    } else if (IsInternalBuffer(buffer, buf_len)) {
      memmove(next_write_location, buffer, buf_len);
    } else {
      QUIC_BUG << "Buffer[" << static_cast<const void*>(buffer) << ", "
               << static_cast<const void*>(buffer + buf_len)
               << ") overlaps with internal buffer["
               << static_cast<const void*>(buffer_) << ", "
               << static_cast<const void*>(buffer_end()) << ")";
      return result;
    }
    result.buffer_copied = true;
  } else {
    // In place push, do nothing.
  }
  buffered_writes_.emplace_back(
      next_write_location, buf_len, self_address, peer_address,
      options ? options->Clone() : std::unique_ptr<PerPacketOptions>());

  DCHECK(Invariants());

  result.succeeded = true;
  return result;
}

QuicBatchWriterBuffer::PopResult QuicBatchWriterBuffer::PopBufferedWrite(
    int32_t num_buffered_writes) {
  DCHECK(Invariants());
  DCHECK_GE(num_buffered_writes, 0);
  DCHECK_LE(num_buffered_writes, static_cast<int>(buffered_writes_.size()));

  PopResult result = {/*num_buffers_popped=*/0,
                      /*moved_remaining_buffers=*/false};

  result.num_buffers_popped = std::max<int32_t>(num_buffered_writes, 0);
  result.num_buffers_popped =
      std::min<int32_t>(result.num_buffers_popped, buffered_writes_.size());
  buffered_writes_.erase(buffered_writes_.begin(),
                         buffered_writes_.begin() + result.num_buffers_popped);

  if (!buffered_writes_.empty()) {
    // If not all buffered writes are erased, the remaining ones will not cover
    // a continuous prefix of buffer_. We'll fix it by moving the remaining
    // buffers to the beginning of buffer_ and adjust the buffer pointers in all
    // remaining buffered writes.
    // This should happen very rarely, about once per write block.
    result.moved_remaining_buffers = true;
    const char* buffer_before_move = buffered_writes_.begin()->buffer;
    size_t buffer_len_to_move = buffered_writes_.rbegin()->buffer +
                                buffered_writes_.rbegin()->buf_len -
                                buffer_before_move;
    memmove(buffer_, buffer_before_move, buffer_len_to_move);

    size_t distance_to_move = buffer_before_move - buffer_;
    for (BufferedWrite& buffered_write : buffered_writes_) {
      buffered_write.buffer -= distance_to_move;
    }

    DCHECK_EQ(buffer_, buffered_writes_.begin()->buffer);
  }
  DCHECK(Invariants());

  return result;
}

size_t QuicBatchWriterBuffer::SizeInUse() const {
  if (buffered_writes_.empty()) {
    return 0;
  }

  return buffered_writes_.back().buffer + buffered_writes_.back().buf_len -
         buffer_;
}

}  // namespace quic
