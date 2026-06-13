// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/batch_writer/quic_batch_writer_buffer.h"

#include <algorithm>
#include <sstream>
#include <string>

namespace quic {

QuicBatchWriterBuffer::QuicBatchWriterBuffer() {
  memset(buffer_, 0, sizeof(buffer_));
}

void QuicBatchWriterBuffer::Clear() { buffered_writes_.clear(); }

std::string QuicBatchWriterBuffer::DebugString() const {
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

  return static_cast<size_t>(next_buffer - buffer_) == SizeInUse();
}

char* QuicBatchWriterBuffer::GetNextWriteLocation() const {
  const char* next_loc =
      buffered_writes_.empty()
          ? buffer_
          : buffered_writes_.back().buffer + buffered_writes_.back().buf_len;
  if (static_cast<size_t>(buffer_end() - next_loc) < kMaxOutgoingPacketSize) {
    return nullptr;
  }
  return const_cast<char*>(next_loc);
}

QuicBatchWriterBuffer::PushResult QuicBatchWriterBuffer::PushBufferedWrite(
    const char* buffer, size_t buf_len, const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address, const PerPacketOptions* options,
    const QuicPacketWriterParams& params, uint64_t release_time) {
  QUICHE_DCHECK(Invariants());
  QUICHE_DCHECK_LE(buf_len, kMaxOutgoingPacketSize);

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
      QUIC_BUG(quic_bug_10831_1)
          << "Buffer[" << static_cast<const void*>(buffer) << ", "
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
  if (buffered_writes_.empty()) {
    // Starting a new batch.
    ++batch_id_;

    // |batch_id| is a 32-bit unsigned int that is possibly shared by a lot of
    // QUIC connections(because writer can be shared), so wrap around happens,
    // when it happens we skip id=0, which indicates "not batched".
    if (batch_id_ == 0) {
      ++batch_id_;
    }
  }
  buffered_writes_.emplace_back(
      next_write_location, buf_len, self_address, peer_address,
      options ? options->Clone() : std::unique_ptr<PerPacketOptions>(), params,
      release_time);

  QUICHE_DCHECK(Invariants());

  result.succeeded = true;
  result.batch_id = batch_id_;
  return result;
}

void QuicBatchWriterBuffer::UndoLastPush() {
  if (!buffered_writes_.empty()) {
    buffered_writes_.pop_back();
  }
}

QuicBatchWriterBuffer::PopResult QuicBatchWriterBuffer::PopBufferedWrite(
    int32_t num_buffered_writes) {
  QUICHE_DCHECK(Invariants());
  QUICHE_DCHECK_GE(num_buffered_writes, 0);
  QUICHE_DCHECK_LE(static_cast<size_t>(num_buffered_writes),
                   buffered_writes_.size());

  PopResult result = {/*num_buffers_popped=*/0,
                      /*moved_remaining_buffers=*/false};

  result.num_buffers_popped = std::max<int32_t>(num_buffered_writes, 0);
  result.num_buffers_popped =
      std::min<int32_t>(result.num_buffers_popped, buffered_writes_.size());
  buffered_writes_.pop_front_n(result.num_buffers_popped);

  if (!buffered_writes_.empty()) {
    // If not all buffered writes are erased, the remaining ones will not cover
    // a continuous prefix of buffer_. We'll fix it by moving the remaining
    // buffers to the beginning of buffer_ and adjust the buffer pointers in all
    // remaining buffered writes.
    // This should happen very rarely, about once per write block.
    result.moved_remaining_buffers = true;
    const char* buffer_before_move = buffered_writes_.front().buffer;
    size_t buffer_len_to_move = buffered_writes_.back().buffer +
                                buffered_writes_.back().buf_len -
                                buffer_before_move;
    memmove(buffer_, buffer_before_move, buffer_len_to_move);

    size_t distance_to_move = buffer_before_move - buffer_;
    for (BufferedWrite& buffered_write : buffered_writes_) {
      buffered_write.buffer -= distance_to_move;
    }

    QUICHE_DCHECK_EQ(buffer_, buffered_writes_.front().buffer);
  }
  QUICHE_DCHECK(Invariants());

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
