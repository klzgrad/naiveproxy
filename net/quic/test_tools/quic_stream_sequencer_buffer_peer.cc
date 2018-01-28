// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/quic_stream_sequencer_buffer_peer.h"

#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_test.h"
#include "net/test/gtest_util.h"

typedef net::QuicStreamSequencerBuffer::BufferBlock BufferBlock;
typedef net::QuicStreamSequencerBuffer::FrameInfo FrameInfo;
typedef net::QuicStreamSequencerBuffer::Gap Gap;

static const size_t kBlockSizeBytes =
    net::QuicStreamSequencerBuffer::kBlockSizeBytes;

namespace net {
namespace test {

QuicStreamSequencerBufferPeer::QuicStreamSequencerBufferPeer(
    QuicStreamSequencerBuffer* buffer)
    : buffer_(buffer) {}

// Read from this buffer_ into the given destination buffer_ up to the
// size of the destination. Returns the number of bytes read. Reading from
// an empty buffer_->returns 0.
size_t QuicStreamSequencerBufferPeer::Read(char* dest_buffer, size_t size) {
  iovec dest;
  dest.iov_base = dest_buffer, dest.iov_len = size;
  size_t bytes_read;
  std::string error_details;
  EXPECT_EQ(QUIC_NO_ERROR,
            buffer_->Readv(&dest, 1, &bytes_read, &error_details));
  return bytes_read;
}

// If buffer is empty, the blocks_ array must be empty, which means all
// blocks are deallocated.
bool QuicStreamSequencerBufferPeer::CheckEmptyInvariants() {
  return !buffer_->Empty() || IsBlockArrayEmpty();
}

bool QuicStreamSequencerBufferPeer::IsBlockArrayEmpty() {
  if (buffer_->blocks_ == nullptr) {
    return true;
  }

  size_t count = buffer_->blocks_count_;
  for (size_t i = 0; i < count; i++) {
    if (buffer_->blocks_[i] != nullptr) {
      return false;
    }
  }
  return true;
}

bool QuicStreamSequencerBufferPeer::CheckInitialState() {
  EXPECT_TRUE(buffer_->Empty() && buffer_->total_bytes_read_ == 0 &&
              buffer_->num_bytes_buffered_ == 0);
  return CheckBufferInvariants();
}

bool QuicStreamSequencerBufferPeer::CheckBufferInvariants() {
  QuicStreamOffset data_span =
      buffer_->gaps_.back().begin_offset - buffer_->total_bytes_read_;
  bool capacity_sane = data_span <= buffer_->max_buffer_capacity_bytes_ &&
                       data_span >= buffer_->num_bytes_buffered_;
  if (!capacity_sane) {
    QUIC_LOG(ERROR) << "data span is larger than capacity.";
    QUIC_LOG(ERROR) << "total read: " << buffer_->total_bytes_read_
                    << " last byte: " << buffer_->gaps_.back().begin_offset;
  }
  bool total_read_sane =
      buffer_->gaps_.front().begin_offset >= buffer_->total_bytes_read_;
  if (!total_read_sane) {
    QUIC_LOG(ERROR) << "read across 1st gap.";
  }
  bool read_offset_sane = buffer_->ReadOffset() < kBlockSizeBytes;
  if (!capacity_sane) {
    QUIC_LOG(ERROR) << "read offset go beyond 1st block";
  }
  bool block_match_capacity = (buffer_->max_buffer_capacity_bytes_ <=
                               buffer_->blocks_count_ * kBlockSizeBytes) &&
                              (buffer_->max_buffer_capacity_bytes_ >
                               (buffer_->blocks_count_ - 1) * kBlockSizeBytes);
  if (!capacity_sane) {
    QUIC_LOG(ERROR) << "block number not match capcaity.";
  }
  bool block_retired_when_empty = CheckEmptyInvariants();
  if (!block_retired_when_empty) {
    QUIC_LOG(ERROR) << "block is not retired after use.";
  }
  return capacity_sane && total_read_sane && read_offset_sane &&
         block_match_capacity && block_retired_when_empty;
}

size_t QuicStreamSequencerBufferPeer::GetInBlockOffset(
    QuicStreamOffset offset) {
  return buffer_->GetInBlockOffset(offset);
}

BufferBlock* QuicStreamSequencerBufferPeer::GetBlock(size_t index) {
  return buffer_->blocks_[index];
}

int QuicStreamSequencerBufferPeer::GapSize() {
  return buffer_->gaps_.size();
}

std::list<Gap> QuicStreamSequencerBufferPeer::GetGaps() {
  return buffer_->gaps_;
}

size_t QuicStreamSequencerBufferPeer::max_buffer_capacity() {
  return buffer_->max_buffer_capacity_bytes_;
}

size_t QuicStreamSequencerBufferPeer::ReadableBytes() {
  return buffer_->ReadableBytes();
}

std::map<QuicStreamOffset, FrameInfo>*
QuicStreamSequencerBufferPeer::frame_arrival_time_map() {
  return &(buffer_->frame_arrival_time_map_);
}

void QuicStreamSequencerBufferPeer::set_total_bytes_read(
    QuicStreamOffset total_bytes_read) {
  buffer_->total_bytes_read_ = total_bytes_read;
}

void QuicStreamSequencerBufferPeer::set_gaps(const std::list<Gap>& gaps) {
  buffer_->gaps_ = gaps;
}

bool QuicStreamSequencerBufferPeer::IsBufferAllocated() {
  return buffer_->blocks_ != nullptr;
}

}  // namespace test
}  // namespace net
