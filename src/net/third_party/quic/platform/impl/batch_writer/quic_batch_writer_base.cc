// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/batch_writer/quic_batch_writer_base.h"

#include "net/third_party/quic/platform/api/quic_ptr_util.h"

namespace quic {

QuicBatchWriterBase::QuicBatchWriterBase(
    std::unique_ptr<QuicBatchWriterBuffer> batch_buffer)
    : write_blocked_(false), batch_buffer_(std::move(batch_buffer)) {}

WriteResult QuicBatchWriterBase::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* options) {
  const WriteResult result =
      InternalWritePacket(buffer, buf_len, self_address, peer_address, options);
  if (result.status == WRITE_STATUS_BLOCKED) {
    write_blocked_ = true;
  }
  return result;
}

WriteResult QuicBatchWriterBase::InternalWritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* options) {
  if (buf_len > kMaxPacketSize) {
    return WriteResult(WRITE_STATUS_MSG_TOO_BIG, EMSGSIZE);
  }

  const CanBatchResult can_batch_result =
      CanBatch(buffer, buf_len, self_address, peer_address, options);

  bool buffered = false;
  bool flush = can_batch_result.must_flush;

  if (can_batch_result.can_batch) {
    QuicBatchWriterBuffer::PushResult push_result =
        batch_buffer_->PushBufferedWrite(buffer, buf_len, self_address,
                                         peer_address, options);
    if (push_result.succeeded) {
      buffered = true;
      // If there's no space left after the packet is buffered, force a flush.
      flush = flush || (batch_buffer_->GetNextWriteLocation() == nullptr);
    } else {
      // If there's no space without this packet, force a flush.
      flush = true;
    }
  }

  if (!flush) {
    return WriteResult(WRITE_STATUS_OK, 0);
  }

  size_t num_buffered_packets = buffered_writes().size();
  const FlushImplResult flush_result = CheckedFlush();
  const WriteResult& result = flush_result.write_result;
  QUIC_DVLOG(1) << "Internally flushed " << flush_result.num_packets_sent
                << " out of " << num_buffered_packets
                << " packets. WriteResult=" << result;

  if (result.status != WRITE_STATUS_OK) {
    if (result.status == WRITE_STATUS_BLOCKED && buffered) {
      // Return OK if buffered successfully but write blocked while flush. The
      // caller should handle write blockage by checking if IsWriteBlocked().
      return WriteResult(WRITE_STATUS_OK, 0);
    }
    return result;
  }

  if (!buffered) {
    QuicBatchWriterBuffer::PushResult push_result =
        batch_buffer_->PushBufferedWrite(buffer, buf_len, self_address,
                                         peer_address, options);
    buffered = push_result.succeeded;

    // Since buffered_writes has been emptied, this write must have been
    // buffered successfully.
    QUIC_BUG_IF(!buffered) << "Failed to push to an empty batch buffer."
                           << "  self_addr:" << self_address.ToString()
                           << ", peer_addr:" << peer_address.ToString()
                           << ", buf_len:" << buf_len;
  }

  return result;
}

QuicBatchWriterBase::FlushImplResult QuicBatchWriterBase::CheckedFlush() {
  if (buffered_writes().empty()) {
    return FlushImplResult{WriteResult(WRITE_STATUS_OK, 0),
                           /*num_packets_sent=*/0, /*bytes_written=*/0};
  }

  const FlushImplResult flush_result = FlushImpl();

  // Either flush_result.write_result.status is not WRITE_STATUS_OK, or it is
  // WRITE_STATUS_OK and batch_buffer is empty.
  DCHECK(flush_result.write_result.status != WRITE_STATUS_OK ||
         buffered_writes().empty());

  return flush_result;
}

WriteResult QuicBatchWriterBase::Flush() {
  size_t num_buffered_packets = buffered_writes().size();
  const FlushImplResult flush_result = CheckedFlush();
  QUIC_DVLOG(1) << "Externally flushed " << flush_result.num_packets_sent
                << " out of " << num_buffered_packets
                << " packets. WriteResult=" << flush_result.write_result;

  if (flush_result.write_result.status == WRITE_STATUS_BLOCKED) {
    write_blocked_ = true;
  }
  return flush_result.write_result;
}

}  // namespace quic
