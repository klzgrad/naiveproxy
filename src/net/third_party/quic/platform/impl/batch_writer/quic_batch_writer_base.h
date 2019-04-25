// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_BATCH_WRITER_BASE_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_BATCH_WRITER_BASE_H_

#include "net/third_party/quic/core/quic_packet_writer.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quic/platform/impl/batch_writer/quic_batch_writer_buffer.h"

namespace quic {

// QuicBatchWriterBase implements logic common to all derived batch writers,
// including maintaining write blockage state and a skeleton implemention of
// WritePacket().
// A derived batch writer must override the FlushImpl() function to send all
// buffered writes in a batch. It must also override the CanBatch() function
// to control whether/when a WritePacket() call should flush.
class QuicBatchWriterBase : public QuicPacketWriter {
 public:
  explicit QuicBatchWriterBase(
      std::unique_ptr<QuicBatchWriterBuffer> batch_buffer);

  WriteResult WritePacket(const char* buffer,
                          size_t buf_len,
                          const QuicIpAddress& self_address,
                          const QuicSocketAddress& peer_address,
                          PerPacketOptions* options) override;

  bool IsWriteBlocked() const final { return write_blocked_; }

  void SetWritable() final { write_blocked_ = false; }

  QuicByteCount GetMaxPacketSize(
      const QuicSocketAddress& peer_address) const final {
    return kMaxPacketSize;
  }

  bool SupportsReleaseTime() const final { return false; }

  bool IsBatchMode() const final { return true; }

  char* GetNextWriteLocation(const QuicIpAddress& self_address,
                             const QuicSocketAddress& peer_address) final {
    return batch_buffer_->GetNextWriteLocation();
  }

  WriteResult Flush() final;

 protected:
  const QuicBatchWriterBuffer& batch_buffer() const { return *batch_buffer_; }
  QuicBatchWriterBuffer& batch_buffer() { return *batch_buffer_; }

  const QuicDeque<BufferedWrite>& buffered_writes() const {
    return batch_buffer_->buffered_writes();
  }

  struct CanBatchResult {
    CanBatchResult(bool can_batch, bool must_flush)
        : can_batch(can_batch), must_flush(must_flush) {}
    // Whether this write can be batched with existing buffered writes.
    bool can_batch;
    // If |can_batch|, whether the caller must flush after this packet is
    // buffered.
    // Always true if not |can_batch|.
    bool must_flush;
  };

  // Given the existing buffered writes(in buffered_writes()), whether a new
  // write(in the arguments) can be batched.
  virtual CanBatchResult CanBatch(const char* buffer,
                                  size_t buf_len,
                                  const QuicIpAddress& self_address,
                                  const QuicSocketAddress& peer_address,
                                  const PerPacketOptions* options) const = 0;

  struct FlushImplResult {
    // The return value of the Flush() interface, which is:
    // - WriteResult(WRITE_STATUS_OK, <bytes_flushed>) if all buffered writes
    //   were sent successfully.
    // - WRITE_STATUS_BLOCKED or WRITE_STATUS_ERROR, if the batch write is
    //   blocked or returned an error while sending. If a portion of buffered
    //   writes were sent successfully, |FlushImplResult.num_packets_sent| and
    //   |FlushImplResult.bytes_written| contain the number of successfully sent
    //   packets and their total bytes.
    WriteResult write_result;
    int num_packets_sent;
    // If write_result.status == WRITE_STATUS_OK, |bytes_written| will be equal
    // to write_result.bytes_written. Otherwise |bytes_written| will be the
    // number of bytes written before WRITE_BLOCK or WRITE_ERROR happened.
    int bytes_written;
  };

  // Send all buffered writes(in buffered_writes()) in a batch.
  // buffered_writes() is guaranteed to be non-empty when this function is
  // called.
  virtual FlushImplResult FlushImpl() = 0;

 private:
  WriteResult InternalWritePacket(const char* buffer,
                                  size_t buf_len,
                                  const QuicIpAddress& self_address,
                                  const QuicSocketAddress& peer_address,
                                  PerPacketOptions* options);

  // Calls FlushImpl() and check its post condition.
  FlushImplResult CheckedFlush();

  bool write_blocked_;
  std::unique_ptr<QuicBatchWriterBuffer> batch_buffer_;
};

// QuicUdpBatchWriter is a batch writer backed by a UDP socket.
class QuicUdpBatchWriter : public QuicBatchWriterBase {
 public:
  QuicUdpBatchWriter(std::unique_ptr<QuicBatchWriterBuffer> batch_buffer,
                     int fd)
      : QuicBatchWriterBase(std::move(batch_buffer)), fd_(fd) {}

  int fd() const { return fd_; }

 private:
  const int fd_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_BATCH_WRITER_QUIC_BATCH_WRITER_BASE_H_
