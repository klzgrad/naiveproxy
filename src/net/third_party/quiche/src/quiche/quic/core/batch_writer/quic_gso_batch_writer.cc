// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/batch_writer/quic_gso_batch_writer.h"

#include <time.h>

#include <ctime>

#include "quiche/quic/core/quic_linux_socket_utils.h"
#include "quiche/quic/platform/api/quic_server_stats.h"

namespace quic {

// static
std::unique_ptr<QuicBatchWriterBuffer>
QuicGsoBatchWriter::CreateBatchWriterBuffer() {
  return std::make_unique<QuicBatchWriterBuffer>();
}

QuicGsoBatchWriter::QuicGsoBatchWriter(int fd)
    : QuicGsoBatchWriter(fd, CLOCK_MONOTONIC) {}

QuicGsoBatchWriter::QuicGsoBatchWriter(int fd,
                                       clockid_t clockid_for_release_time)
    : QuicUdpBatchWriter(CreateBatchWriterBuffer(), fd),
      clockid_for_release_time_(clockid_for_release_time),
      supports_release_time_(
          GetQuicRestartFlag(quic_support_release_time_for_gso) &&
          QuicLinuxSocketUtils::EnableReleaseTime(fd,
                                                  clockid_for_release_time)) {
  if (supports_release_time_) {
    QUIC_RESTART_FLAG_COUNT(quic_support_release_time_for_gso);
    QUIC_LOG_FIRST_N(INFO, 5) << "Release time is enabled.";
  } else {
    QUIC_LOG_FIRST_N(INFO, 5) << "Release time is not enabled.";
  }
}

QuicGsoBatchWriter::QuicGsoBatchWriter(
    std::unique_ptr<QuicBatchWriterBuffer> batch_buffer, int fd,
    clockid_t clockid_for_release_time, ReleaseTimeForceEnabler /*enabler*/)
    : QuicUdpBatchWriter(std::move(batch_buffer), fd),
      clockid_for_release_time_(clockid_for_release_time),
      supports_release_time_(true) {
  QUIC_DLOG(INFO) << "Release time forcefully enabled.";
}

QuicGsoBatchWriter::CanBatchResult QuicGsoBatchWriter::CanBatch(
    const char* /*buffer*/, size_t buf_len, const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address, const PerPacketOptions* options,
    uint64_t release_time) const {
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
  // [5] The new packet can be released without delay, or it has the same
  //     release time as buffered writes.
  const BufferedWrite& first = buffered_writes().front();
  const BufferedWrite& last = buffered_writes().back();
  // Whether this packet can be sent without delay, regardless of release time.
  const bool can_burst = !SupportsReleaseTime() || !options ||
                         options->release_time_delay.IsZero() ||
                         options->allow_burst;
  size_t max_segments = MaxSegments(first.buf_len);
  bool can_batch =
      buffered_writes().size() < max_segments &&                    // [0]
      last.self_address == self_address &&                          // [1]
      last.peer_address == peer_address &&                          // [1]
      batch_buffer().SizeInUse() + buf_len <= kMaxGsoPacketSize &&  // [2]
      first.buf_len == last.buf_len &&                              // [3]
      first.buf_len >= buf_len &&                                   // [4]
      (can_burst || first.release_time == release_time);            // [5]

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

QuicGsoBatchWriter::ReleaseTime QuicGsoBatchWriter::GetReleaseTime(
    const PerPacketOptions* options) const {
  QUICHE_DCHECK(SupportsReleaseTime());

  if (options == nullptr) {
    return {0, QuicTime::Delta::Zero()};
  }

  const uint64_t now = NowInNanosForReleaseTime();
  const uint64_t ideal_release_time =
      now + options->release_time_delay.ToMicroseconds() * 1000;

  if ((options->release_time_delay.IsZero() || options->allow_burst) &&
      !buffered_writes().empty() &&
      // If release time of buffered packets is in the past, flush buffered
      // packets and buffer this packet at the ideal release time.
      (buffered_writes().back().release_time >= now)) {
    // Send as soon as possible, but no sooner than the last buffered packet.
    const uint64_t actual_release_time = buffered_writes().back().release_time;

    const int64_t offset_ns = actual_release_time - ideal_release_time;
    ReleaseTime result{actual_release_time,
                       QuicTime::Delta::FromMicroseconds(offset_ns / 1000)};

    QUIC_DVLOG(1) << "ideal_release_time:" << ideal_release_time
                  << ", actual_release_time:" << actual_release_time
                  << ", offset:" << result.release_time_offset;
    return result;
  }

  // Send according to the release time delay.
  return {ideal_release_time, QuicTime::Delta::Zero()};
}

uint64_t QuicGsoBatchWriter::NowInNanosForReleaseTime() const {
  struct timespec ts;

  if (clock_gettime(clockid_for_release_time_, &ts) != 0) {
    return 0;
  }

  return ts.tv_sec * (1000ULL * 1000 * 1000) + ts.tv_nsec;
}

// static
void QuicGsoBatchWriter::BuildCmsg(QuicMsgHdr* hdr,
                                   const QuicIpAddress& self_address,
                                   uint16_t gso_size, uint64_t release_time) {
  hdr->SetIpInNextCmsg(self_address);
  if (gso_size > 0) {
    *hdr->GetNextCmsgData<uint16_t>(SOL_UDP, UDP_SEGMENT) = gso_size;
  }
  if (release_time != 0) {
    *hdr->GetNextCmsgData<uint64_t>(SOL_SOCKET, SO_TXTIME) = release_time;
  }
}

QuicGsoBatchWriter::FlushImplResult QuicGsoBatchWriter::FlushImpl() {
  return InternalFlushImpl<kCmsgSpace>(BuildCmsg);
}

}  // namespace quic
