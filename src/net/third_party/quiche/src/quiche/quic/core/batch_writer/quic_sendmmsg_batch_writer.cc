// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/batch_writer/quic_sendmmsg_batch_writer.h"

namespace quic {

QuicSendmmsgBatchWriter::QuicSendmmsgBatchWriter(
    std::unique_ptr<QuicBatchWriterBuffer> batch_buffer, int fd)
    : QuicUdpBatchWriter(std::move(batch_buffer), fd) {}

QuicSendmmsgBatchWriter::CanBatchResult QuicSendmmsgBatchWriter::CanBatch(
    const char* /*buffer*/, size_t /*buf_len*/,
    const QuicIpAddress& /*self_address*/,
    const QuicSocketAddress& /*peer_address*/,
    const PerPacketOptions* /*options*/,
    const QuicPacketWriterParams& /*params*/, uint64_t /*release_time*/) const {
  return CanBatchResult(/*can_batch=*/true, /*must_flush=*/false);
}

QuicSendmmsgBatchWriter::FlushImplResult QuicSendmmsgBatchWriter::FlushImpl() {
  return InternalFlushImpl(
      kCmsgSpaceForIp,
      [](QuicMMsgHdr* mhdr, int i, const BufferedWrite& buffered_write) {
        mhdr->SetIpInNextCmsg(i, buffered_write.self_address);
      });
}

QuicSendmmsgBatchWriter::FlushImplResult
QuicSendmmsgBatchWriter::InternalFlushImpl(size_t cmsg_space,
                                           const CmsgBuilder& cmsg_builder) {
  QUICHE_DCHECK(!IsWriteBlocked());
  QUICHE_DCHECK(!buffered_writes().empty());

  FlushImplResult result = {WriteResult(WRITE_STATUS_OK, 0),
                            /*num_packets_sent=*/0, /*bytes_written=*/0};
  WriteResult& write_result = result.write_result;

  auto first = buffered_writes().cbegin();
  const auto last = buffered_writes().cend();
  while (first != last) {
    QuicMMsgHdr mhdr(first, last, cmsg_space, cmsg_builder);

    int num_packets_sent;
    write_result = QuicLinuxSocketUtils::WriteMultiplePackets(
        fd(), &mhdr, &num_packets_sent);
    QUIC_DVLOG(1) << "WriteMultiplePackets sent " << num_packets_sent
                  << " out of " << mhdr.num_msgs()
                  << " packets. WriteResult=" << write_result;

    if (write_result.status != WRITE_STATUS_OK) {
      QUICHE_DCHECK_EQ(0, num_packets_sent);
      break;
    } else if (num_packets_sent == 0) {
      QUIC_BUG(quic_bug_10825_1)
          << "WriteMultiplePackets returned OK, but no packets were sent.";
      write_result = WriteResult(WRITE_STATUS_ERROR, EIO);
      break;
    }

    first += num_packets_sent;

    result.num_packets_sent += num_packets_sent;
    result.bytes_written += write_result.bytes_written;
  }

  // Call PopBufferedWrite() even if write_result.status is not WRITE_STATUS_OK,
  // to deal with partial writes.
  batch_buffer().PopBufferedWrite(result.num_packets_sent);

  if (write_result.status != WRITE_STATUS_OK) {
    return result;
  }

  QUIC_BUG_IF(quic_bug_12537_1, !buffered_writes().empty())
      << "All packets should have been written on a successful return";
  write_result.bytes_written = result.bytes_written;
  return result;
}

}  // namespace quic
