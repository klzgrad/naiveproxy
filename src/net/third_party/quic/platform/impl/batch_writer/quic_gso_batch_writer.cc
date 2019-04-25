// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/batch_writer/quic_gso_batch_writer.h"

#include "net/third_party/quic/platform/impl/quic_linux_socket_utils.h"

namespace quic {

QuicGsoBatchWriter::QuicGsoBatchWriter(
    std::unique_ptr<QuicBatchWriterBuffer> batch_buffer,
    int fd)
    : QuicUdpBatchWriter(std::move(batch_buffer), fd) {}

QuicGsoBatchWriter::CanBatchResult QuicGsoBatchWriter::CanBatch(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    const PerPacketOptions* /*options*/) const {
  // If there is nothing buffered already, this write will be included in this
  // batch.
  if (buffered_writes().empty()) {
    return CanBatchResult(/*can_batch=*/true, /*must_flush=*/false);
  }

  // The new write can be batched if all of the following are true:
  // [0] The total number of the GSO segments(one write=one segment, including
  //     the new write) must not exceed |max_segments|.
  // [1] It has the same source and destination addresses as already buffered
  //     writes.
  // [2] It won't cause this batch to exceed kMaxGsoPacketSize.
  // [3] Already buffered writes all have the same length.
  // [4] Length of already buffered writes must >= length of the new write.
  const BufferedWrite& first = buffered_writes().front();
  const BufferedWrite& last = buffered_writes().back();
  size_t max_segments = MaxSegments(first.buf_len);
  bool can_batch =
      buffered_writes().size() < max_segments &&                    // [0]
      last.self_address == self_address &&                          // [1]
      last.peer_address == peer_address &&                          // [1]
      batch_buffer().SizeInUse() + buf_len <= kMaxGsoPacketSize &&  // [2]
      first.buf_len == last.buf_len &&                              // [3]
      first.buf_len >= buf_len;                                     // [4]

  // A flush is required if any of the following is true:
  // [a] The new write can't be batched.
  // [b] Length of the new write is different from the length of already
  //     buffered writes.
  // [c] The total number of the GSO segments, including the new write, reaches
  //     |max_segments|.
  bool must_flush = (!can_batch) ||                                  // [a]
                    (last.buf_len != buf_len) ||                     // [b]
                    (buffered_writes().size() + 1 == max_segments);  // [c]
  return CanBatchResult(can_batch, must_flush);
}

// static
void QuicGsoBatchWriter::BuildCmsg(QuicMsgHdr* hdr,
                                   const QuicIpAddress& self_address,
                                   uint16_t gso_size) {
  hdr->SetIpInNextCmsg(self_address);
  if (gso_size > 0) {
    *hdr->GetNextCmsgData<uint16_t>(SOL_UDP, UDP_SEGMENT) = gso_size;
  }
}

QuicGsoBatchWriter::FlushImplResult QuicGsoBatchWriter::FlushImpl() {
  return InternalFlushImpl<kCmsgSpace>(BuildCmsg);
}

}  // namespace quic
