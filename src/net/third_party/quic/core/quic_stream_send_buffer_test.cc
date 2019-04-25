// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_stream_send_buffer.h"

#include "net/third_party/quic/core/quic_data_writer.h"
#include "net/third_party/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/test_tools/quic_stream_send_buffer_peer.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {
namespace {

struct iovec MakeIovec(QuicStringPiece data) {
  struct iovec iov = {const_cast<char*>(data.data()),
                      static_cast<size_t>(data.size())};
  return iov;
}

class QuicStreamSendBufferTest : public QuicTest {
 public:
  QuicStreamSendBufferTest() : send_buffer_(&allocator_) {
    EXPECT_EQ(0u, send_buffer_.size());
    EXPECT_EQ(0u, send_buffer_.stream_bytes_written());
    EXPECT_EQ(0u, send_buffer_.stream_bytes_outstanding());
    QuicString data1(1536, 'a');
    QuicString data2 = QuicString(256, 'b') + QuicString(256, 'c');
    struct iovec iov[2];
    iov[0] = MakeIovec(QuicStringPiece(data1));
    iov[1] = MakeIovec(QuicStringPiece(data2));

    QuicMemSlice slice1(&allocator_, 1024);
    memset(const_cast<char*>(slice1.data()), 'c', 1024);
    QuicMemSlice slice2(&allocator_, 768);
    memset(const_cast<char*>(slice2.data()), 'd', 768);

    // Index starts from not pointing to any slice.
    EXPECT_EQ(nullptr,
              QuicStreamSendBufferPeer::CurrentWriteSlice(&send_buffer_));

    // Save all data.
    SetQuicFlag(&FLAGS_quic_send_buffer_max_data_slice_size, 1024);
    send_buffer_.SaveStreamData(iov, 2, 0, 2048);
    send_buffer_.SaveMemSlice(std::move(slice1));
    EXPECT_TRUE(slice1.empty());
    send_buffer_.SaveMemSlice(std::move(slice2));
    EXPECT_TRUE(slice2.empty());

    EXPECT_EQ(4u, send_buffer_.size());
    // At this point, the whole buffer looks like:
    // |      a * 1536      |b * 256|         c * 1280        |  d * 768  |
    // |    slice1     |     slice2       |      slice3       |   slice4  |
  }

  void WriteAllData() {
    // Write all data.
    char buf[4000];
    QuicDataWriter writer(4000, buf, HOST_BYTE_ORDER);
    send_buffer_.WriteStreamData(0, 3840u, &writer);

    send_buffer_.OnStreamDataConsumed(3840u);
    EXPECT_EQ(3840u, send_buffer_.stream_bytes_written());
    EXPECT_EQ(3840u, send_buffer_.stream_bytes_outstanding());
  }

  SimpleBufferAllocator allocator_;
  QuicStreamSendBuffer send_buffer_;
};

TEST_F(QuicStreamSendBufferTest, CopyDataToBuffer) {
  char buf[4000];
  QuicDataWriter writer(4000, buf, HOST_BYTE_ORDER);
  QuicString copy1(1024, 'a');
  QuicString copy2 =
      QuicString(512, 'a') + QuicString(256, 'b') + QuicString(256, 'c');
  QuicString copy3(1024, 'c');
  QuicString copy4(768, 'd');

  ASSERT_TRUE(send_buffer_.WriteStreamData(0, 1024, &writer));
  EXPECT_EQ(copy1, QuicStringPiece(buf, 1024));
  ASSERT_TRUE(send_buffer_.WriteStreamData(1024, 1024, &writer));
  EXPECT_EQ(copy2, QuicStringPiece(buf + 1024, 1024));
  ASSERT_TRUE(send_buffer_.WriteStreamData(2048, 1024, &writer));
  EXPECT_EQ(copy3, QuicStringPiece(buf + 2048, 1024));
  ASSERT_TRUE(send_buffer_.WriteStreamData(3072, 768, &writer));
  EXPECT_EQ(copy4, QuicStringPiece(buf + 3072, 768));

  // Test data piece across boundries.
  QuicDataWriter writer2(4000, buf, HOST_BYTE_ORDER);
  QuicString copy5 =
      QuicString(536, 'a') + QuicString(256, 'b') + QuicString(232, 'c');
  ASSERT_TRUE(send_buffer_.WriteStreamData(1000, 1024, &writer2));
  EXPECT_EQ(copy5, QuicStringPiece(buf, 1024));
  ASSERT_TRUE(send_buffer_.WriteStreamData(2500, 1024, &writer2));
  QuicString copy6 = QuicString(572, 'c') + QuicString(452, 'd');
  EXPECT_EQ(copy6, QuicStringPiece(buf + 1024, 1024));

  // Invalid data copy.
  QuicDataWriter writer3(4000, buf, HOST_BYTE_ORDER);
  EXPECT_FALSE(send_buffer_.WriteStreamData(3000, 1024, &writer3));
  EXPECT_QUIC_BUG(send_buffer_.WriteStreamData(0, 4000, &writer3),
                  "Writer fails to write.");

  send_buffer_.OnStreamDataConsumed(3840);
  EXPECT_EQ(3840u, send_buffer_.stream_bytes_written());
  EXPECT_EQ(3840u, send_buffer_.stream_bytes_outstanding());
}

TEST_F(QuicStreamSendBufferTest, RemoveStreamFrame) {
  WriteAllData();

  QuicByteCount newly_acked_length;
  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(1024, 1024, &newly_acked_length));
  EXPECT_EQ(1024u, newly_acked_length);
  EXPECT_EQ(4u, send_buffer_.size());

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(2048, 1024, &newly_acked_length));
  EXPECT_EQ(1024u, newly_acked_length);
  EXPECT_EQ(4u, send_buffer_.size());

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(0, 1024, &newly_acked_length));
  EXPECT_EQ(1024u, newly_acked_length);

  // Send buffer is cleaned up in order.
  EXPECT_EQ(1u, send_buffer_.size());
  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(3072, 768, &newly_acked_length));
  EXPECT_EQ(768u, newly_acked_length);
  EXPECT_EQ(0u, send_buffer_.size());
}

TEST_F(QuicStreamSendBufferTest, RemoveStreamFrameAcrossBoundries) {
  WriteAllData();

  QuicByteCount newly_acked_length;
  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(2024, 576, &newly_acked_length));
  EXPECT_EQ(576u, newly_acked_length);
  EXPECT_EQ(4u, send_buffer_.size());

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(0, 1000, &newly_acked_length));
  EXPECT_EQ(1000u, newly_acked_length);
  EXPECT_EQ(4u, send_buffer_.size());

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(1000, 1024, &newly_acked_length));
  EXPECT_EQ(1024u, newly_acked_length);
  // Send buffer is cleaned up in order.
  EXPECT_EQ(2u, send_buffer_.size());

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(2600, 1024, &newly_acked_length));
  EXPECT_EQ(1024u, newly_acked_length);
  EXPECT_EQ(1u, send_buffer_.size());

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(3624, 216, &newly_acked_length));
  EXPECT_EQ(216u, newly_acked_length);
  EXPECT_EQ(0u, send_buffer_.size());
}

TEST_F(QuicStreamSendBufferTest, AckStreamDataMultipleTimes) {
  WriteAllData();
  QuicByteCount newly_acked_length;
  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(100, 1500, &newly_acked_length));
  EXPECT_EQ(1500u, newly_acked_length);
  EXPECT_EQ(4u, send_buffer_.size());

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(2000, 500, &newly_acked_length));
  EXPECT_EQ(500u, newly_acked_length);
  EXPECT_EQ(4u, send_buffer_.size());

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(0, 2600, &newly_acked_length));
  EXPECT_EQ(600u, newly_acked_length);
  // Send buffer is cleaned up in order.
  EXPECT_EQ(2u, send_buffer_.size());

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(2200, 1640, &newly_acked_length));
  EXPECT_EQ(1240u, newly_acked_length);
  EXPECT_EQ(0u, send_buffer_.size());

  EXPECT_FALSE(send_buffer_.OnStreamDataAcked(4000, 100, &newly_acked_length));
}

TEST_F(QuicStreamSendBufferTest, AckStreamDataOutOfOrder) {
  WriteAllData();
  QuicByteCount newly_acked_length;
  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(500, 1000, &newly_acked_length));
  EXPECT_EQ(1000u, newly_acked_length);
  EXPECT_EQ(4u, send_buffer_.size());
  EXPECT_EQ(3840u, QuicStreamSendBufferPeer::TotalLength(&send_buffer_));

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(1200, 1000, &newly_acked_length));
  EXPECT_EQ(700u, newly_acked_length);
  EXPECT_EQ(4u, send_buffer_.size());
  // Slice 2 gets fully acked.
  EXPECT_EQ(2816u, QuicStreamSendBufferPeer::TotalLength(&send_buffer_));

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(2000, 1840, &newly_acked_length));
  EXPECT_EQ(1640u, newly_acked_length);
  EXPECT_EQ(4u, send_buffer_.size());
  // Slices 3 and 4 get fully acked.
  EXPECT_EQ(1024u, QuicStreamSendBufferPeer::TotalLength(&send_buffer_));

  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(0, 1000, &newly_acked_length));
  EXPECT_EQ(500u, newly_acked_length);
  EXPECT_EQ(0u, send_buffer_.size());
  EXPECT_EQ(0u, QuicStreamSendBufferPeer::TotalLength(&send_buffer_));
}

TEST_F(QuicStreamSendBufferTest, PendingRetransmission) {
  WriteAllData();
  EXPECT_TRUE(send_buffer_.IsStreamDataOutstanding(0, 3840));
  EXPECT_FALSE(send_buffer_.HasPendingRetransmission());
  // Lost data [0, 1200).
  send_buffer_.OnStreamDataLost(0, 1200);
  // Lost data [1500, 2000).
  send_buffer_.OnStreamDataLost(1500, 500);
  EXPECT_TRUE(send_buffer_.HasPendingRetransmission());

  EXPECT_EQ(StreamPendingRetransmission(0, 1200),
            send_buffer_.NextPendingRetransmission());
  // Retransmit data [0, 500).
  send_buffer_.OnStreamDataRetransmitted(0, 500);
  EXPECT_TRUE(send_buffer_.IsStreamDataOutstanding(0, 500));
  EXPECT_EQ(StreamPendingRetransmission(500, 700),
            send_buffer_.NextPendingRetransmission());
  // Ack data [500, 1200).
  QuicByteCount newly_acked_length = 0;
  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(500, 700, &newly_acked_length));
  EXPECT_FALSE(send_buffer_.IsStreamDataOutstanding(500, 700));
  EXPECT_TRUE(send_buffer_.HasPendingRetransmission());
  EXPECT_EQ(StreamPendingRetransmission(1500, 500),
            send_buffer_.NextPendingRetransmission());
  // Retransmit data [1500, 2000).
  send_buffer_.OnStreamDataRetransmitted(1500, 500);
  EXPECT_FALSE(send_buffer_.HasPendingRetransmission());

  // Lost [200, 800).
  send_buffer_.OnStreamDataLost(200, 600);
  EXPECT_TRUE(send_buffer_.HasPendingRetransmission());
  // Verify [200, 500) is considered as lost, as [500, 800) has been acked.
  EXPECT_EQ(StreamPendingRetransmission(200, 300),
            send_buffer_.NextPendingRetransmission());

  // Verify 0 length data is not outstanding.
  EXPECT_FALSE(send_buffer_.IsStreamDataOutstanding(100, 0));
  // Verify partially acked data is outstanding.
  EXPECT_TRUE(send_buffer_.IsStreamDataOutstanding(400, 800));
}

TEST_F(QuicStreamSendBufferTest, CurrentWriteIndex) {
  char buf[4000];
  QuicDataWriter writer(4000, buf, HOST_BYTE_ORDER);
  // With data buffered, index points to the 1st slice of data.
  EXPECT_EQ(0u,
            QuicStreamSendBufferPeer::CurrentWriteSlice(&send_buffer_)->offset);
  ASSERT_TRUE(send_buffer_.WriteStreamData(0, 1024, &writer));
  // Wrote all data on 1st slice, index points to next slice.
  EXPECT_EQ(1024u,
            QuicStreamSendBufferPeer::CurrentWriteSlice(&send_buffer_)->offset);
  ASSERT_TRUE(send_buffer_.WriteStreamData(1024, 512, &writer));
  // Last write didn't finish a whole slice. Index remains.
  EXPECT_EQ(1024u,
            QuicStreamSendBufferPeer::CurrentWriteSlice(&send_buffer_)->offset);
  send_buffer_.OnStreamDataConsumed(1024);

  // If data in 1st slice gets ACK'ed, it shouldn't change the indexed slice
  QuicByteCount newly_acked_length;
  EXPECT_TRUE(send_buffer_.OnStreamDataAcked(0, 1024, &newly_acked_length));
  EXPECT_EQ(1024u,
            QuicStreamSendBufferPeer::CurrentWriteSlice(&send_buffer_)->offset);

  ASSERT_TRUE(
      send_buffer_.WriteStreamData(1024 + 512, 3840 - 1024 - 512, &writer));
  // After writing all buffered data, index become invalid again.
  EXPECT_EQ(nullptr,
            QuicStreamSendBufferPeer::CurrentWriteSlice(&send_buffer_));
  QuicMemSlice slice(&allocator_, 60);
  memset(const_cast<char*>(slice.data()), 'e', 60);
  send_buffer_.SaveMemSlice(std::move(slice));
  // With new data, index points to the new data.
  EXPECT_EQ(3840u,
            QuicStreamSendBufferPeer::CurrentWriteSlice(&send_buffer_)->offset);
}

}  // namespace
}  // namespace test
}  // namespace quic
