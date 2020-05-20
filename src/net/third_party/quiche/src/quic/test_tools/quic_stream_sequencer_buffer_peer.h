// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_SEQUENCER_BUFFER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_SEQUENCER_BUFFER_PEER_H_

#include "net/third_party/quiche/src/quic/core/quic_stream_sequencer_buffer.h"

namespace quic {

namespace test {

class QuicStreamSequencerBufferPeer {
 public:
  explicit QuicStreamSequencerBufferPeer(QuicStreamSequencerBuffer* buffer);
  QuicStreamSequencerBufferPeer(const QuicStreamSequencerBufferPeer&) = delete;
  QuicStreamSequencerBufferPeer& operator=(
      const QuicStreamSequencerBufferPeer&) = delete;

  // Read from this buffer_ into the given destination buffer_ up to the
  // size of the destination. Returns the number of bytes read. Reading from
  // an empty buffer_->returns 0.
  size_t Read(char* dest_buffer, size_t size);

  // If buffer is empty, the blocks_ array must be empty, which means all
  // blocks are deallocated.
  bool CheckEmptyInvariants();

  bool IsBlockArrayEmpty();

  bool CheckInitialState();

  bool CheckBufferInvariants();

  size_t GetInBlockOffset(QuicStreamOffset offset);

  QuicStreamSequencerBuffer::BufferBlock* GetBlock(size_t index);

  int IntervalSize();

  size_t max_buffer_capacity();

  size_t ReadableBytes();

  void set_total_bytes_read(QuicStreamOffset total_bytes_read);

  void AddBytesReceived(QuicStreamOffset offset, QuicByteCount length);

  bool IsBufferAllocated();

  size_t block_count();

  const QuicIntervalSet<QuicStreamOffset>& bytes_received();

 private:
  QuicStreamSequencerBuffer* buffer_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_SEQUENCER_BUFFER_PEER_H_
