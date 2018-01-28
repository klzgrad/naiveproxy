// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_STREAM_SEQUENCER_BUFFER_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_STREAM_SEQUENCER_BUFFER_PEER_H_

#include "net/quic/core/quic_stream_sequencer_buffer.h"

namespace net {

namespace test {

class QuicStreamSequencerBufferPeer {
 public:
  explicit QuicStreamSequencerBufferPeer(QuicStreamSequencerBuffer* buffer);

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

  int GapSize();

  std::list<QuicStreamSequencerBuffer::Gap> GetGaps();

  size_t max_buffer_capacity();

  size_t ReadableBytes();

  std::map<QuicStreamOffset, QuicStreamSequencerBuffer::FrameInfo>*
  frame_arrival_time_map();

  void set_total_bytes_read(QuicStreamOffset total_bytes_read);

  void set_gaps(const std::list<QuicStreamSequencerBuffer::Gap>& gaps);

  bool IsBufferAllocated();

 private:
  QuicStreamSequencerBuffer* buffer_;
  DISALLOW_COPY_AND_ASSIGN(QuicStreamSequencerBufferPeer);
};

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_STREAM_SEQUENCER_BUFFER_PEER_H_
