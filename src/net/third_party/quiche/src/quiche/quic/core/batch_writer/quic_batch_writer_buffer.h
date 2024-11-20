// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_BATCH_WRITER_QUIC_BATCH_WRITER_BUFFER_H_
#define QUICHE_QUIC_CORE_BATCH_WRITER_QUIC_BATCH_WRITER_BUFFER_H_

#include "absl/base/optimization.h"
#include "quiche/quic/core/quic_linux_socket_utils.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/quiche_circular_deque.h"

namespace quic {

// QuicBatchWriterBuffer manages an internal buffer to hold data from multiple
// packets. Packet data are placed continuously within the internal buffer such
// that they can be sent by a QuicGsoBatchWriter.
// This class can also be used by a QuicBatchWriter which uses sendmmsg,
// although it is not optimized for that use case.
class QUICHE_EXPORT QuicBatchWriterBuffer {
 public:
  QuicBatchWriterBuffer();

  // Clear all buffered writes, but leave the internal buffer intact.
  void Clear();

  char* GetNextWriteLocation() const;

  // Push a buffered write to the back.
  struct QUICHE_EXPORT PushResult {
    bool succeeded;
    // True in one of the following cases:
    // 1) The packet buffer is external and copied to the internal buffer, or
    // 2) The packet buffer is from the internal buffer and moved within it.
    //    This only happens if PopBufferedWrite is called in the middle of a
    //    in-place push.
    // Only valid if |succeeded| is true.
    bool buffer_copied;
    // The batch ID of the packet. Only valid if |succeeded|.
    uint32_t batch_id = 0;
  };

  PushResult PushBufferedWrite(const char* buffer, size_t buf_len,
                               const QuicIpAddress& self_address,
                               const QuicSocketAddress& peer_address,
                               const PerPacketOptions* options,
                               const QuicPacketWriterParams& params,
                               uint64_t release_time);

  void UndoLastPush();

  // Pop |num_buffered_writes| buffered writes from the front.
  // |num_buffered_writes| will be capped to [0, buffered_writes().size()]
  // before it is used.
  struct QUICHE_EXPORT PopResult {
    int32_t num_buffers_popped;
    // True if after |num_buffers_popped| buffers are popped from front, the
    // remaining buffers are moved to the beginning of the internal buffer.
    // This should normally be false.
    bool moved_remaining_buffers;
  };
  PopResult PopBufferedWrite(int32_t num_buffered_writes);

  const quiche::QuicheCircularDeque<BufferedWrite>& buffered_writes() const {
    return buffered_writes_;
  }

  bool IsExternalBuffer(const char* buffer, size_t buf_len) const {
    return (buffer + buf_len) <= buffer_ || buffer >= buffer_end();
  }
  bool IsInternalBuffer(const char* buffer, size_t buf_len) const {
    return buffer >= buffer_ && (buffer + buf_len) <= buffer_end();
  }

  // Number of bytes used in |buffer_|.
  // PushBufferedWrite() increases this; PopBufferedWrite decreases this.
  size_t SizeInUse() const;

  // Rounded up from |kMaxGsoPacketSize|, which is the maximum allowed
  // size of a GSO packet.
  static const size_t kBufferSize = 64 * 1024;

  std::string DebugString() const;

 protected:
  // Whether the invariants of the buffer are upheld. For debug & test only.
  bool Invariants() const;
  const char* buffer_end() const { return buffer_ + sizeof(buffer_); }
  ABSL_CACHELINE_ALIGNED char buffer_[kBufferSize];
  quiche::QuicheCircularDeque<BufferedWrite> buffered_writes_;
  // 0 if a batch has never started. Otherwise
  // - If |buffered_writes_| is empty, this is the ID of the previous batch.
  // - If |buffered_writes_| is not empty, this is the ID of the current batch.
  // For debugging only.
  uint32_t batch_id_ = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_BATCH_WRITER_QUIC_BATCH_WRITER_BUFFER_H_
