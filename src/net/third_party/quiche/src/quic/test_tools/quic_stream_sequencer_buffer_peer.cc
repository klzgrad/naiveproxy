// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_stream_sequencer_buffer_peer.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

typedef quic::QuicStreamSequencerBuffer::BufferBlock BufferBlock;

static const size_t kBlockSizeBytes =
    quic::QuicStreamSequencerBuffer::kBlockSizeBytes;

namespace quic {
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
  EXPECT_THAT(buffer_->Readv(&dest, 1, &bytes_read, &error_details),
              IsQuicNoError());
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
      buffer_->NextExpectedByte() - buffer_->total_bytes_read_;
  bool capacity_sane = data_span <= buffer_->max_buffer_capacity_bytes_ &&
                       data_span >= buffer_->num_bytes_buffered_;
  if (!capacity_sane) {
    QUIC_LOG(ERROR) << "data span is larger than capacity.";
    QUIC_LOG(ERROR) << "total read: " << buffer_->total_bytes_read_
                    << " last byte: " << buffer_->NextExpectedByte();
  }
  bool total_read_sane =
      buffer_->FirstMissingByte() >= buffer_->total_bytes_read_;
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

int QuicStreamSequencerBufferPeer::IntervalSize() {
  if (buffer_->bytes_received_.Empty()) {
    return 1;
  }
  int gap_size = buffer_->bytes_received_.Size() + 1;
  if (buffer_->bytes_received_.Empty()) {
    return gap_size;
  }
  if (buffer_->bytes_received_.begin()->min() == 0) {
    --gap_size;
  }
  if (buffer_->bytes_received_.rbegin()->max() ==
      std::numeric_limits<uint64_t>::max()) {
    --gap_size;
  }
  return gap_size;
}

size_t QuicStreamSequencerBufferPeer::max_buffer_capacity() {
  return buffer_->max_buffer_capacity_bytes_;
}

size_t QuicStreamSequencerBufferPeer::ReadableBytes() {
  return buffer_->ReadableBytes();
}

void QuicStreamSequencerBufferPeer::set_total_bytes_read(
    QuicStreamOffset total_bytes_read) {
  buffer_->total_bytes_read_ = total_bytes_read;
}

void QuicStreamSequencerBufferPeer::AddBytesReceived(QuicStreamOffset offset,
                                                     QuicByteCount length) {
  buffer_->bytes_received_.Add(offset, offset + length);
}

bool QuicStreamSequencerBufferPeer::IsBufferAllocated() {
  return buffer_->blocks_ != nullptr;
}

size_t QuicStreamSequencerBufferPeer::block_count() {
  return buffer_->blocks_count_;
}

const QuicIntervalSet<QuicStreamOffset>&
QuicStreamSequencerBufferPeer::bytes_received() {
  return buffer_->bytes_received_;
}

}  // namespace test
}  // namespace quic
