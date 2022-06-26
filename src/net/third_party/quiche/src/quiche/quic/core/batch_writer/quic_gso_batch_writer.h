// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_BATCH_WRITER_QUIC_GSO_BATCH_WRITER_H_
#define QUICHE_QUIC_CORE_BATCH_WRITER_QUIC_GSO_BATCH_WRITER_H_

#include "quiche/quic/core/batch_writer/quic_batch_writer_base.h"

namespace quic {

// QuicGsoBatchWriter sends QUIC packets in batches, using UDP socket's generic
// segmentation offload(GSO) capability.
class QUIC_EXPORT_PRIVATE QuicGsoBatchWriter : public QuicUdpBatchWriter {
 public:
  explicit QuicGsoBatchWriter(int fd);

  // |clockid_for_release_time|: FQ qdisc requires CLOCK_MONOTONIC, EDF requires
  // CLOCK_TAI.
  QuicGsoBatchWriter(int fd, clockid_t clockid_for_release_time);

  bool SupportsReleaseTime() const final { return supports_release_time_; }

  CanBatchResult CanBatch(const char* buffer, size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          const PerPacketOptions* options,
                          uint64_t release_time) const override;

  FlushImplResult FlushImpl() override;

 protected:
  // Test only constructor to forcefully enable release time.
  struct QUIC_EXPORT_PRIVATE ReleaseTimeForceEnabler {};
  QuicGsoBatchWriter(std::unique_ptr<QuicBatchWriterBuffer> batch_buffer,
                     int fd, clockid_t clockid_for_release_time,
                     ReleaseTimeForceEnabler enabler);

  ReleaseTime GetReleaseTime(const PerPacketOptions* options) const override;

  // Get the current time in nanos from |clockid_for_release_time_|.
  virtual uint64_t NowInNanosForReleaseTime() const;

  static size_t MaxSegments(size_t gso_size) {
    // Max segments should be the min of UDP_MAX_SEGMENTS(64) and
    // (((64KB - sizeof(ip hdr) - sizeof(udp hdr)) / MSS) + 1), in the typical
    // case of IPv6 packets with 1500-byte MTU, the result is
    //         ((64KB - 40 - 8) / (1500 - 48)) + 1 = 46
    // However, due a kernel bug, the limit is much lower for tiny gso_sizes.
    return gso_size <= 2 ? 16 : 45;
  }

  static const int kCmsgSpace =
      kCmsgSpaceForIp + kCmsgSpaceForSegmentSize + kCmsgSpaceForTxTime;
  static void BuildCmsg(QuicMsgHdr* hdr, const QuicIpAddress& self_address,
                        uint16_t gso_size, uint64_t release_time);

  template <size_t CmsgSpace, typename CmsgBuilderT>
  FlushImplResult InternalFlushImpl(CmsgBuilderT cmsg_builder) {
    QUICHE_DCHECK(!IsWriteBlocked());
    QUICHE_DCHECK(!buffered_writes().empty());

    FlushImplResult result = {WriteResult(WRITE_STATUS_OK, 0),
                              /*num_packets_sent=*/0, /*bytes_written=*/0};
    WriteResult& write_result = result.write_result;

    int total_bytes = batch_buffer().SizeInUse();
    const BufferedWrite& first = buffered_writes().front();
    char cbuf[CmsgSpace];
    QuicMsgHdr hdr(first.buffer, total_bytes, first.peer_address, cbuf,
                   sizeof(cbuf));

    uint16_t gso_size = buffered_writes().size() > 1 ? first.buf_len : 0;
    cmsg_builder(&hdr, first.self_address, gso_size, first.release_time);

    write_result = QuicLinuxSocketUtils::WritePacket(fd(), hdr);
    QUIC_DVLOG(1) << "Write GSO packet result: " << write_result
                  << ", fd: " << fd()
                  << ", self_address: " << first.self_address.ToString()
                  << ", peer_address: " << first.peer_address.ToString()
                  << ", num_segments: " << buffered_writes().size()
                  << ", total_bytes: " << total_bytes
                  << ", gso_size: " << gso_size
                  << ", release_time: " << first.release_time;

    // All segments in a GSO packet share the same fate - if the write failed,
    // none of them are sent, and it's not needed to call PopBufferedWrite().
    if (write_result.status != WRITE_STATUS_OK) {
      return result;
    }

    result.num_packets_sent = buffered_writes().size();

    write_result.bytes_written = total_bytes;
    result.bytes_written = total_bytes;

    batch_buffer().PopBufferedWrite(buffered_writes().size());

    QUIC_BUG_IF(quic_bug_12544_1, !buffered_writes().empty())
        << "All packets should have been written on a successful return";
    return result;
  }

 private:
  static std::unique_ptr<QuicBatchWriterBuffer> CreateBatchWriterBuffer();

  const clockid_t clockid_for_release_time_;
  const bool supports_release_time_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_BATCH_WRITER_QUIC_GSO_BATCH_WRITER_H_
