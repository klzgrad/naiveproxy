// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/batch_writer/quic_batch_writer_buffer.h"
#include <memory>
#include <string>

#include "quic/core/quic_constants.h"
#include "quic/platform/api/quic_ip_address.h"
#include "quic/platform/api/quic_socket_address.h"
#include "quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QUIC_EXPORT_PRIVATE TestQuicBatchWriterBuffer
    : public QuicBatchWriterBuffer {
 public:
  using QuicBatchWriterBuffer::buffer_;
  using QuicBatchWriterBuffer::buffered_writes_;
};

static const size_t kBatchBufferSize = QuicBatchWriterBuffer::kBufferSize;

class QuicBatchWriterBufferTest : public QuicTest {
 public:
  QuicBatchWriterBufferTest() { SwitchToNewBuffer(); }

  void SwitchToNewBuffer() {
    batch_buffer_ = std::make_unique<TestQuicBatchWriterBuffer>();
  }

  // Fill packet_buffer_ with kMaxOutgoingPacketSize bytes of |c|s.
  char* FillPacketBuffer(char c) {
    return FillPacketBuffer(c, packet_buffer_, kMaxOutgoingPacketSize);
  }

  // Fill |packet_buffer| with kMaxOutgoingPacketSize bytes of |c|s.
  char* FillPacketBuffer(char c, char* packet_buffer) {
    return FillPacketBuffer(c, packet_buffer, kMaxOutgoingPacketSize);
  }

  // Fill |packet_buffer| with |buf_len| bytes of |c|s.
  char* FillPacketBuffer(char c, char* packet_buffer, size_t buf_len) {
    memset(packet_buffer, c, buf_len);
    return packet_buffer;
  }

  void CheckBufferedWriteContent(int buffered_write_index,
                                 char buffer_content,
                                 size_t buf_len,
                                 const QuicIpAddress& self_addr,
                                 const QuicSocketAddress& peer_addr,
                                 const PerPacketOptions* options) {
    const BufferedWrite& buffered_write =
        batch_buffer_->buffered_writes()[buffered_write_index];
    EXPECT_EQ(buf_len, buffered_write.buf_len);
    for (size_t i = 0; i < buf_len; ++i) {
      EXPECT_EQ(buffer_content, buffered_write.buffer[i]);
      if (buffer_content != buffered_write.buffer[i]) {
        break;
      }
    }
    EXPECT_EQ(self_addr, buffered_write.self_address);
    EXPECT_EQ(peer_addr, buffered_write.peer_address);
    if (options == nullptr) {
      EXPECT_EQ(nullptr, buffered_write.options);
    } else {
      EXPECT_EQ(options->release_time_delay,
                buffered_write.options->release_time_delay);
    }
  }

 protected:
  std::unique_ptr<TestQuicBatchWriterBuffer> batch_buffer_;
  QuicIpAddress self_addr_;
  QuicSocketAddress peer_addr_;
  uint64_t release_time_ = 0;
  char packet_buffer_[kMaxOutgoingPacketSize];
};

class BufferSizeSequence {
 public:
  explicit BufferSizeSequence(
      std::vector<std::pair<std::vector<size_t>, size_t>> stages)
      : stages_(std::move(stages)),
        total_buf_len_(0),
        stage_index_(0),
        sequence_index_(0) {}

  size_t Next() {
    const std::vector<size_t>& seq = stages_[stage_index_].first;
    size_t buf_len = seq[sequence_index_++ % seq.size()];
    total_buf_len_ += buf_len;
    if (stages_[stage_index_].second <= total_buf_len_) {
      stage_index_ = std::min(stage_index_ + 1, stages_.size() - 1);
    }
    return buf_len;
  }

 private:
  const std::vector<std::pair<std::vector<size_t>, size_t>> stages_;
  size_t total_buf_len_;
  size_t stage_index_;
  size_t sequence_index_;
};

// Test in-place pushes. A in-place push is a push with a buffer address that is
// equal to the result of GetNextWriteLocation().
TEST_F(QuicBatchWriterBufferTest, InPlacePushes) {
  std::vector<BufferSizeSequence> buffer_size_sequences = {
      // Push large writes until the buffer is near full, then switch to 1-byte
      // writes. This covers the edge cases when detecting insufficient buffer.
      BufferSizeSequence({{{1350}, kBatchBufferSize - 3000}, {{1}, 1e6}}),
      // A sequence that looks real.
      BufferSizeSequence({{{1, 39, 97, 150, 1350, 1350, 1350, 1350}, 1e6}}),
  };

  for (auto& buffer_size_sequence : buffer_size_sequences) {
    SwitchToNewBuffer();
    int64_t num_push_failures = 0;

    while (batch_buffer_->SizeInUse() < kBatchBufferSize) {
      size_t buf_len = buffer_size_sequence.Next();
      const bool has_enough_space =
          (kBatchBufferSize - batch_buffer_->SizeInUse() >=
           kMaxOutgoingPacketSize);

      char* buffer = batch_buffer_->GetNextWriteLocation();

      if (has_enough_space) {
        EXPECT_EQ(batch_buffer_->buffer_ + batch_buffer_->SizeInUse(), buffer);
      } else {
        EXPECT_EQ(nullptr, buffer);
      }

      SCOPED_TRACE(testing::Message()
                   << "Before Push: buf_len=" << buf_len
                   << ", has_enough_space=" << has_enough_space
                   << ", batch_buffer=" << batch_buffer_->DebugString());

      auto push_result = batch_buffer_->PushBufferedWrite(
          buffer, buf_len, self_addr_, peer_addr_, nullptr, release_time_);
      if (!push_result.succeeded) {
        ++num_push_failures;
      }
      EXPECT_EQ(has_enough_space, push_result.succeeded);
      EXPECT_FALSE(push_result.buffer_copied);
      if (!has_enough_space) {
        break;
      }
    }
    // Expect one and only one failure from the final push operation.
    EXPECT_EQ(1, num_push_failures);
  }
}

// Test some in-place pushes mixed with pushes with external buffers.
TEST_F(QuicBatchWriterBufferTest, MixedPushes) {
  // First, a in-place push.
  char* buffer = batch_buffer_->GetNextWriteLocation();
  auto push_result = batch_buffer_->PushBufferedWrite(
      FillPacketBuffer('A', buffer), kDefaultMaxPacketSize, self_addr_,
      peer_addr_, nullptr, release_time_);
  EXPECT_TRUE(push_result.succeeded);
  EXPECT_FALSE(push_result.buffer_copied);
  CheckBufferedWriteContent(0, 'A', kDefaultMaxPacketSize, self_addr_,
                            peer_addr_, nullptr);

  // Then a push with external buffer.
  push_result = batch_buffer_->PushBufferedWrite(
      FillPacketBuffer('B'), kDefaultMaxPacketSize, self_addr_, peer_addr_,
      nullptr, release_time_);
  EXPECT_TRUE(push_result.succeeded);
  EXPECT_TRUE(push_result.buffer_copied);
  CheckBufferedWriteContent(1, 'B', kDefaultMaxPacketSize, self_addr_,
                            peer_addr_, nullptr);

  // Then another in-place push.
  buffer = batch_buffer_->GetNextWriteLocation();
  push_result = batch_buffer_->PushBufferedWrite(
      FillPacketBuffer('C', buffer), kDefaultMaxPacketSize, self_addr_,
      peer_addr_, nullptr, release_time_);
  EXPECT_TRUE(push_result.succeeded);
  EXPECT_FALSE(push_result.buffer_copied);
  CheckBufferedWriteContent(2, 'C', kDefaultMaxPacketSize, self_addr_,
                            peer_addr_, nullptr);

  // Then another push with external buffer.
  push_result = batch_buffer_->PushBufferedWrite(
      FillPacketBuffer('D'), kDefaultMaxPacketSize, self_addr_, peer_addr_,
      nullptr, release_time_);
  EXPECT_TRUE(push_result.succeeded);
  EXPECT_TRUE(push_result.buffer_copied);
  CheckBufferedWriteContent(3, 'D', kDefaultMaxPacketSize, self_addr_,
                            peer_addr_, nullptr);
}

TEST_F(QuicBatchWriterBufferTest, PopAll) {
  const int kNumBufferedWrites = 10;
  for (int i = 0; i < kNumBufferedWrites; ++i) {
    EXPECT_TRUE(batch_buffer_
                    ->PushBufferedWrite(packet_buffer_, kDefaultMaxPacketSize,
                                        self_addr_, peer_addr_, nullptr,
                                        release_time_)
                    .succeeded);
  }
  EXPECT_EQ(kNumBufferedWrites,
            static_cast<int>(batch_buffer_->buffered_writes().size()));

  auto pop_result = batch_buffer_->PopBufferedWrite(kNumBufferedWrites);
  EXPECT_EQ(0u, batch_buffer_->buffered_writes().size());
  EXPECT_EQ(kNumBufferedWrites, pop_result.num_buffers_popped);
  EXPECT_FALSE(pop_result.moved_remaining_buffers);
}

TEST_F(QuicBatchWriterBufferTest, PopPartial) {
  const int kNumBufferedWrites = 10;
  for (int i = 0; i < kNumBufferedWrites; ++i) {
    EXPECT_TRUE(batch_buffer_
                    ->PushBufferedWrite(FillPacketBuffer('A' + i),
                                        kDefaultMaxPacketSize - i, self_addr_,
                                        peer_addr_, nullptr, release_time_)
                    .succeeded);
  }

  for (size_t i = 0;
       i < kNumBufferedWrites && !batch_buffer_->buffered_writes().empty();
       ++i) {
    const size_t size_before_pop = batch_buffer_->buffered_writes().size();
    const size_t expect_size_after_pop =
        size_before_pop < i ? 0 : size_before_pop - i;
    batch_buffer_->PopBufferedWrite(i);
    ASSERT_EQ(expect_size_after_pop, batch_buffer_->buffered_writes().size());
    const char first_write_content =
        'A' + kNumBufferedWrites - expect_size_after_pop;
    const size_t first_write_len =
        kDefaultMaxPacketSize - kNumBufferedWrites + expect_size_after_pop;
    for (size_t j = 0; j < expect_size_after_pop; ++j) {
      CheckBufferedWriteContent(j, first_write_content + j, first_write_len - j,
                                self_addr_, peer_addr_, nullptr);
    }
  }
}

TEST_F(QuicBatchWriterBufferTest, InPlacePushWithPops) {
  // First, a in-place push.
  char* buffer = batch_buffer_->GetNextWriteLocation();
  const size_t first_packet_len = 2;
  auto push_result = batch_buffer_->PushBufferedWrite(
      FillPacketBuffer('A', buffer, first_packet_len), first_packet_len,
      self_addr_, peer_addr_, nullptr, release_time_);
  EXPECT_TRUE(push_result.succeeded);
  EXPECT_FALSE(push_result.buffer_copied);
  CheckBufferedWriteContent(0, 'A', first_packet_len, self_addr_, peer_addr_,
                            nullptr);

  // Simulate the case where the writer wants to do another in-place push, but
  // can't do so because it can't be batched with the first buffer.
  buffer = batch_buffer_->GetNextWriteLocation();
  const size_t second_packet_len = 1350;

  // Flush the first buffer.
  auto pop_result = batch_buffer_->PopBufferedWrite(1);
  EXPECT_EQ(1, pop_result.num_buffers_popped);
  EXPECT_FALSE(pop_result.moved_remaining_buffers);

  // Now the second push.
  push_result = batch_buffer_->PushBufferedWrite(
      FillPacketBuffer('B', buffer, second_packet_len), second_packet_len,
      self_addr_, peer_addr_, nullptr, release_time_);
  EXPECT_TRUE(push_result.succeeded);
  EXPECT_TRUE(push_result.buffer_copied);
  CheckBufferedWriteContent(0, 'B', second_packet_len, self_addr_, peer_addr_,
                            nullptr);
}

}  // namespace
}  // namespace test
}  // namespace quic
