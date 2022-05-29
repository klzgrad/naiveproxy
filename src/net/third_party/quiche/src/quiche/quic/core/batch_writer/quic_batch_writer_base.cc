// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/batch_writer/quic_batch_writer_base.h"

#include <cstdint>

#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_server_stats.h"

namespace quic {

QuicBatchWriterBase::QuicBatchWriterBase(
    std::unique_ptr<QuicBatchWriterBuffer> batch_buffer)
    : write_blocked_(false), batch_buffer_(std::move(batch_buffer)) {}

WriteResult QuicBatchWriterBase::WritePacket(
    const char* buffer, size_t buf_len, const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address, PerPacketOptions* options) {
  const WriteResult result =
      InternalWritePacket(buffer, buf_len, self_address, peer_address, options);

  if (IsWriteBlockedStatus(result.status)) {
    write_blocked_ = true;
  }

  return result;
}

WriteResult QuicBatchWriterBase::InternalWritePacket(
    const char* buffer, size_t buf_len, const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address, PerPacketOptions* options) {
  if (buf_len > kMaxOutgoingPacketSize) {
    return WriteResult(WRITE_STATUS_MSG_TOO_BIG, EMSGSIZE);
  }

  ReleaseTime release_time{0, QuicTime::Delta::Zero()};
  if (SupportsReleaseTime()) {
    release_time = GetReleaseTime(options);
    if (release_time.release_time_offset >= QuicTime::Delta::Zero()) {
      QUIC_SERVER_HISTOGRAM_TIMES(
          "batch_writer_positive_release_time_offset",
          release_time.release_time_offset.ToMicroseconds(), 1, 100000, 50,
          "Duration from ideal release time to actual "
          "release time, in microseconds.");
    } else {
      QUIC_SERVER_HISTOGRAM_TIMES(
          "batch_writer_negative_release_time_offset",
          -release_time.release_time_offset.ToMicroseconds(), 1, 100000, 50,
          "Duration from actual release time to ideal "
          "release time, in microseconds.");
    }
  }

  const CanBatchResult can_batch_result =
      CanBatch(buffer, buf_len, self_address, peer_address, options,
               release_time.actual_release_time);

  bool buffered = false;
  bool flush = can_batch_result.must_flush;

  if (can_batch_result.can_batch) {
    QuicBatchWriterBuffer::PushResult push_result =
        batch_buffer_->PushBufferedWrite(buffer, buf_len, self_address,
                                         peer_address, options,
                                         release_time.actual_release_time);
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
    WriteResult result(WRITE_STATUS_OK, 0);
    result.send_time_offset = release_time.release_time_offset;
    return result;
  }

  size_t num_buffered_packets = buffered_writes().size();
  const FlushImplResult flush_result = CheckedFlush();
  WriteResult result = flush_result.write_result;
  QUIC_DVLOG(1) << "Internally flushed " << flush_result.num_packets_sent
                << " out of " << num_buffered_packets
                << " packets. WriteResult=" << result;

  if (result.status != WRITE_STATUS_OK) {
    if (IsWriteBlockedStatus(result.status)) {
      return WriteResult(
          buffered ? WRITE_STATUS_BLOCKED_DATA_BUFFERED : WRITE_STATUS_BLOCKED,
          result.error_code);
    }

    // Drop all packets, including the one being written.
    size_t dropped_packets =
        buffered ? buffered_writes().size() : buffered_writes().size() + 1;

    batch_buffer().Clear();
    result.dropped_packets =
        dropped_packets > std::numeric_limits<uint16_t>::max()
            ? std::numeric_limits<uint16_t>::max()
            : static_cast<uint16_t>(dropped_packets);
    return result;
  }

  if (!buffered) {
    QuicBatchWriterBuffer::PushResult push_result =
        batch_buffer_->PushBufferedWrite(buffer, buf_len, self_address,
                                         peer_address, options,
                                         release_time.actual_release_time);
    buffered = push_result.succeeded;

    // Since buffered_writes has been emptied, this write must have been
    // buffered successfully.
    QUIC_BUG_IF(quic_bug_10826_1, !buffered)
        << "Failed to push to an empty batch buffer."
        << "  self_addr:" << self_address.ToString()
        << ", peer_addr:" << peer_address.ToString() << ", buf_len:" << buf_len;
  }

  result.send_time_offset = release_time.release_time_offset;
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
  QUICHE_DCHECK(flush_result.write_result.status != WRITE_STATUS_OK ||
                buffered_writes().empty());

  // Flush should never return WRITE_STATUS_BLOCKED_DATA_BUFFERED.
  QUICHE_DCHECK(flush_result.write_result.status !=
                WRITE_STATUS_BLOCKED_DATA_BUFFERED);

  return flush_result;
}

WriteResult QuicBatchWriterBase::Flush() {
  size_t num_buffered_packets = buffered_writes().size();
  FlushImplResult flush_result = CheckedFlush();
  QUIC_DVLOG(1) << "Externally flushed " << flush_result.num_packets_sent
                << " out of " << num_buffered_packets
                << " packets. WriteResult=" << flush_result.write_result;

  if (IsWriteError(flush_result.write_result.status)) {
    if (buffered_writes().size() > std::numeric_limits<uint16_t>::max()) {
      flush_result.write_result.dropped_packets =
          std::numeric_limits<uint16_t>::max();
    } else {
      flush_result.write_result.dropped_packets =
          static_cast<uint16_t>(buffered_writes().size());
    }
    // Treat all errors as non-retryable fatal errors. Drop all buffered packets
    // to avoid sending them and getting the same error again.
    batch_buffer().Clear();
  }

  if (flush_result.write_result.status == WRITE_STATUS_BLOCKED) {
    write_blocked_ = true;
  }
  return flush_result.write_result;
}

}  // namespace quic
