// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_BATCH_WRITER_BUFFER_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_BATCH_WRITER_BUFFER_H_

#include "net/third_party/quic/core/quic_packet_writer.h"
#include "net/third_party/quic/platform/api/quic_aligned.h"
#include "net/third_party/quic/platform/api/quic_containers.h"
#include "net/third_party/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/impl/quic_linux_socket_utils.h"

namespace quic {

// QuicBatchWriterBuffer manages an internal buffer to hold data from multiple
// packets. Packet data are placed continuously within the internal buffer such
// that they can be sent by a QuicGsoBatchWriter.
// This class can also be used by a QuicBatchWriter which uses sendmmsg,
// although it is not optimized for that use case.
class QuicBatchWriterBuffer {
 public:
  QuicBatchWriterBuffer();

  char* GetNextWriteLocation() const;

  // Push a buffered write to the back.
  struct PushResult {
    bool succeeded;
    // True in one of the following cases:
    // 1) The packet buffer is external and copied to the internal buffer, or
    // 2) The packet buffer is from the internal buffer and moved within it.
    //    This only happens if PopBufferedWrite is called in the middle of a
    //    in-place push.
    // Only valid if |succeeded| is true.
    bool buffer_copied;
  };
  PushResult PushBufferedWrite(const char* buffer,
                               size_t buf_len,
                               const QuicIpAddress& self_address,
                               const QuicSocketAddress& peer_address,
                               const PerPacketOptions* options);

  // Pop |num_buffered_writes| buffered writes from the front.
  // |num_buffered_writes| will be capped to [0, buffered_writes().size()]
  // before it is used.
  struct PopResult {
    int32_t num_buffers_popped;
    // True if after |num_buffers_popped| buffers are popped from front, the
    // remaining buffers are moved to the beginning of the internal buffer.
    // This should normally be false.
    bool moved_remaining_buffers;
  };
  PopResult PopBufferedWrite(int32_t num_buffered_writes);

  const QuicDeque<BufferedWrite>& buffered_writes() const {
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

  QuicString DebugString() const;

 protected:
  // Whether the invariants of the buffer are upheld. For debug & test only.
  bool Invariants() const;
  const char* buffer_end() const { return buffer_ + sizeof(buffer_); }
  QUIC_CACHELINE_ALIGNED char buffer_[kBufferSize];
  QuicDeque<BufferedWrite> buffered_writes_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_BATCH_WRITER_BUFFER_H_
