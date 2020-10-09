// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/batch_writer/quic_gso_batch_writer.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_mock_syscall_wrapper.h"

using testing::_;
using testing::Invoke;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

size_t PacketLength(const msghdr* msg) {
  size_t length = 0;
  for (size_t i = 0; i < msg->msg_iovlen; ++i) {
    length += msg->msg_iov[i].iov_len;
  }
  return length;
}

uint64_t MillisToNanos(uint64_t milliseconds) {
  return milliseconds * 1000000;
}

class QUIC_EXPORT_PRIVATE TestQuicGsoBatchWriter : public QuicGsoBatchWriter {
 public:
  using QuicGsoBatchWriter::batch_buffer;
  using QuicGsoBatchWriter::buffered_writes;
  using QuicGsoBatchWriter::CanBatch;
  using QuicGsoBatchWriter::CanBatchResult;
  using QuicGsoBatchWriter::GetReleaseTime;
  using QuicGsoBatchWriter::MaxSegments;
  using QuicGsoBatchWriter::QuicGsoBatchWriter;
  using QuicGsoBatchWriter::ReleaseTime;

  static std::unique_ptr<TestQuicGsoBatchWriter>
  NewInstanceWithReleaseTimeSupport() {
    return std::unique_ptr<TestQuicGsoBatchWriter>(new TestQuicGsoBatchWriter(
        std::make_unique<QuicBatchWriterBuffer>(),
        /*fd=*/-1, CLOCK_MONOTONIC, ReleaseTimeForceEnabler()));
  }

  uint64_t NowInNanosForReleaseTime() const override {
    return MillisToNanos(forced_release_time_ms_);
  }

  void ForceReleaseTimeMs(uint64_t forced_release_time_ms) {
    forced_release_time_ms_ = forced_release_time_ms;
  }

 private:
  uint64_t forced_release_time_ms_ = 1;
};

struct QUIC_EXPORT_PRIVATE TestPerPacketOptions : public PerPacketOptions {
  std::unique_ptr<quic::PerPacketOptions> Clone() const override {
    return std::make_unique<TestPerPacketOptions>(*this);
  }
};

// TestBufferedWrite is a copy-constructible BufferedWrite.
struct QUIC_EXPORT_PRIVATE TestBufferedWrite : public BufferedWrite {
  using BufferedWrite::BufferedWrite;
  TestBufferedWrite(const TestBufferedWrite& other)
      : BufferedWrite(other.buffer,
                      other.buf_len,
                      other.self_address,
                      other.peer_address,
                      other.options ? other.options->Clone()
                                    : std::unique_ptr<PerPacketOptions>(),
                      other.release_time) {}
};

// Pointed to by all instances of |BatchCriteriaTestData|. Content not used.
static char unused_packet_buffer[kMaxOutgoingPacketSize];

struct QUIC_EXPORT_PRIVATE BatchCriteriaTestData {
  BatchCriteriaTestData(size_t buf_len,
                        const QuicIpAddress& self_address,
                        const QuicSocketAddress& peer_address,
                        uint64_t release_time,
                        bool can_batch,
                        bool must_flush)
      : buffered_write(unused_packet_buffer,
                       buf_len,
                       self_address,
                       peer_address,
                       std::unique_ptr<PerPacketOptions>(),
                       release_time),
        can_batch(can_batch),
        must_flush(must_flush) {}

  TestBufferedWrite buffered_write;
  // Expected value of CanBatchResult.can_batch when batching |buffered_write|.
  bool can_batch;
  // Expected value of CanBatchResult.must_flush when batching |buffered_write|.
  bool must_flush;
};

std::vector<BatchCriteriaTestData> BatchCriteriaTestData_SizeDecrease() {
  const QuicIpAddress self_addr;
  const QuicSocketAddress peer_addr;
  std::vector<BatchCriteriaTestData> test_data_table = {
      // clang-format off
  // buf_len   self_addr   peer_addr   t_rel   can_batch       must_flush
    {1350,     self_addr,  peer_addr,  0,      true,           false},
    {1350,     self_addr,  peer_addr,  0,      true,           false},
    {1350,     self_addr,  peer_addr,  0,      true,           false},
    {39,       self_addr,  peer_addr,  0,      true,           true},
    {39,       self_addr,  peer_addr,  0,      false,          true},
    {1350,     self_addr,  peer_addr,  0,      false,          true},
      // clang-format on
  };
  return test_data_table;
}

std::vector<BatchCriteriaTestData> BatchCriteriaTestData_SizeIncrease() {
  const QuicIpAddress self_addr;
  const QuicSocketAddress peer_addr;
  std::vector<BatchCriteriaTestData> test_data_table = {
      // clang-format off
  // buf_len   self_addr   peer_addr   t_rel   can_batch       must_flush
    {1350,     self_addr,  peer_addr,  0,      true,           false},
    {1350,     self_addr,  peer_addr,  0,      true,           false},
    {1350,     self_addr,  peer_addr,  0,      true,           false},
    {1351,     self_addr,  peer_addr,  0,      false,          true},
      // clang-format on
  };
  return test_data_table;
}

std::vector<BatchCriteriaTestData> BatchCriteriaTestData_AddressChange() {
  const QuicIpAddress self_addr1 = QuicIpAddress::Loopback4();
  const QuicIpAddress self_addr2 = QuicIpAddress::Loopback6();
  const QuicSocketAddress peer_addr1(self_addr1, 666);
  const QuicSocketAddress peer_addr2(self_addr1, 777);
  const QuicSocketAddress peer_addr3(self_addr2, 666);
  const QuicSocketAddress peer_addr4(self_addr2, 777);
  std::vector<BatchCriteriaTestData> test_data_table = {
      // clang-format off
  // buf_len   self_addr   peer_addr    t_rel  can_batch       must_flush
    {1350,     self_addr1, peer_addr1,  0,     true,           false},
    {1350,     self_addr1, peer_addr1,  0,     true,           false},
    {1350,     self_addr1, peer_addr1,  0,     true,           false},
    {1350,     self_addr2, peer_addr1,  0,     false,          true},
    {1350,     self_addr1, peer_addr2,  0,     false,          true},
    {1350,     self_addr1, peer_addr3,  0,     false,          true},
    {1350,     self_addr1, peer_addr4,  0,     false,          true},
    {1350,     self_addr1, peer_addr4,  0,     false,          true},
      // clang-format on
  };
  return test_data_table;
}

std::vector<BatchCriteriaTestData> BatchCriteriaTestData_ReleaseTime1() {
  const QuicIpAddress self_addr;
  const QuicSocketAddress peer_addr;
  std::vector<BatchCriteriaTestData> test_data_table = {
      // clang-format off
  // buf_len   self_addr   peer_addr   t_rel   can_batch       must_flush
    {1350,     self_addr,  peer_addr,  5,      true,           false},
    {1350,     self_addr,  peer_addr,  5,      true,           false},
    {1350,     self_addr,  peer_addr,  5,      true,           false},
    {1350,     self_addr,  peer_addr,  9,      false,          true},
      // clang-format on
  };
  return test_data_table;
}

std::vector<BatchCriteriaTestData> BatchCriteriaTestData_ReleaseTime2() {
  const QuicIpAddress self_addr;
  const QuicSocketAddress peer_addr;
  std::vector<BatchCriteriaTestData> test_data_table = {
      // clang-format off
  // buf_len   self_addr   peer_addr   t_rel   can_batch       must_flush
    {1350,     self_addr,  peer_addr,  0,      true,           false},
    {1350,     self_addr,  peer_addr,  0,      true,           false},
    {1350,     self_addr,  peer_addr,  0,      true,           false},
    {1350,     self_addr,  peer_addr,  9,      false,          true},
      // clang-format on
  };
  return test_data_table;
}

std::vector<BatchCriteriaTestData> BatchCriteriaTestData_MaxSegments(
    size_t gso_size) {
  const QuicIpAddress self_addr;
  const QuicSocketAddress peer_addr;
  std::vector<BatchCriteriaTestData> test_data_table;
  size_t max_segments = TestQuicGsoBatchWriter::MaxSegments(gso_size);
  for (size_t i = 0; i < max_segments; ++i) {
    bool is_last_in_batch = (i + 1 == max_segments);
    test_data_table.push_back({gso_size, self_addr, peer_addr,
                               /*release_time=*/0, true, is_last_in_batch});
  }
  test_data_table.push_back(
      {gso_size, self_addr, peer_addr, /*release_time=*/0, false, true});
  return test_data_table;
}

class QuicGsoBatchWriterTest : public QuicTest {
 protected:
  WriteResult WritePacket(QuicGsoBatchWriter* writer, size_t packet_size) {
    return writer->WritePacket(&packet_buffer_[0], packet_size, self_address_,
                               peer_address_, nullptr);
  }

  WriteResult WritePacketWithOptions(QuicGsoBatchWriter* writer,
                                     PerPacketOptions* options) {
    return writer->WritePacket(&packet_buffer_[0], 1350, self_address_,
                               peer_address_, options);
  }

  QuicIpAddress self_address_ = QuicIpAddress::Any4();
  QuicSocketAddress peer_address_{QuicIpAddress::Any4(), 443};
  char packet_buffer_[1500];
  StrictMock<MockQuicSyscallWrapper> mock_syscalls_;
  ScopedGlobalSyscallWrapperOverride syscall_override_{&mock_syscalls_};
};

TEST_F(QuicGsoBatchWriterTest, BatchCriteria) {
  std::unique_ptr<TestQuicGsoBatchWriter> writer;

  std::vector<std::vector<BatchCriteriaTestData>> test_data_tables;
  test_data_tables.emplace_back(BatchCriteriaTestData_SizeDecrease());
  test_data_tables.emplace_back(BatchCriteriaTestData_SizeIncrease());
  test_data_tables.emplace_back(BatchCriteriaTestData_AddressChange());
  test_data_tables.emplace_back(BatchCriteriaTestData_ReleaseTime1());
  test_data_tables.emplace_back(BatchCriteriaTestData_ReleaseTime2());
  test_data_tables.emplace_back(BatchCriteriaTestData_MaxSegments(1));
  test_data_tables.emplace_back(BatchCriteriaTestData_MaxSegments(2));
  test_data_tables.emplace_back(BatchCriteriaTestData_MaxSegments(1350));

  for (size_t i = 0; i < test_data_tables.size(); ++i) {
    writer = TestQuicGsoBatchWriter::NewInstanceWithReleaseTimeSupport();

    const auto& test_data_table = test_data_tables[i];
    for (size_t j = 0; j < test_data_table.size(); ++j) {
      const BatchCriteriaTestData& test_data = test_data_table[j];
      SCOPED_TRACE(testing::Message() << "i=" << i << ", j=" << j);
      TestPerPacketOptions options;
      options.release_time_delay = QuicTime::Delta::FromMicroseconds(
          test_data.buffered_write.release_time);
      TestQuicGsoBatchWriter::CanBatchResult result = writer->CanBatch(
          test_data.buffered_write.buffer, test_data.buffered_write.buf_len,
          test_data.buffered_write.self_address,
          test_data.buffered_write.peer_address, &options,
          test_data.buffered_write.release_time);

      ASSERT_EQ(test_data.can_batch, result.can_batch);
      ASSERT_EQ(test_data.must_flush, result.must_flush);

      if (result.can_batch) {
        ASSERT_TRUE(writer->batch_buffer()
                        .PushBufferedWrite(
                            test_data.buffered_write.buffer,
                            test_data.buffered_write.buf_len,
                            test_data.buffered_write.self_address,
                            test_data.buffered_write.peer_address, &options,
                            test_data.buffered_write.release_time)
                        .succeeded);
      }
    }
  }
}

TEST_F(QuicGsoBatchWriterTest, WriteSuccess) {
  TestQuicGsoBatchWriter writer(/*fd=*/-1);

  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 1000));

  EXPECT_CALL(mock_syscalls_, Sendmsg(_, _, _))
      .WillOnce(Invoke([](int /*sockfd*/, const msghdr* msg, int /*flags*/) {
        EXPECT_EQ(1100u, PacketLength(msg));
        return 1100;
      }));
  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 1100), WritePacket(&writer, 100));
  ASSERT_EQ(0u, writer.batch_buffer().SizeInUse());
  ASSERT_EQ(0u, writer.buffered_writes().size());
}

TEST_F(QuicGsoBatchWriterTest, WriteBlockDataNotBuffered) {
  TestQuicGsoBatchWriter writer(/*fd=*/-1);

  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 100));
  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 100));

  EXPECT_CALL(mock_syscalls_, Sendmsg(_, _, _))
      .WillOnce(Invoke([](int /*sockfd*/, const msghdr* msg, int /*flags*/) {
        EXPECT_EQ(200u, PacketLength(msg));
        errno = EWOULDBLOCK;
        return -1;
      }));
  ASSERT_EQ(WriteResult(WRITE_STATUS_BLOCKED, EWOULDBLOCK),
            WritePacket(&writer, 150));
  ASSERT_EQ(200u, writer.batch_buffer().SizeInUse());
  ASSERT_EQ(2u, writer.buffered_writes().size());
}

TEST_F(QuicGsoBatchWriterTest, WriteBlockDataBuffered) {
  TestQuicGsoBatchWriter writer(/*fd=*/-1);

  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 100));
  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 100));

  EXPECT_CALL(mock_syscalls_, Sendmsg(_, _, _))
      .WillOnce(Invoke([](int /*sockfd*/, const msghdr* msg, int /*flags*/) {
        EXPECT_EQ(250u, PacketLength(msg));
        errno = EWOULDBLOCK;
        return -1;
      }));
  ASSERT_EQ(WriteResult(WRITE_STATUS_BLOCKED_DATA_BUFFERED, EWOULDBLOCK),
            WritePacket(&writer, 50));
  ASSERT_EQ(250u, writer.batch_buffer().SizeInUse());
  ASSERT_EQ(3u, writer.buffered_writes().size());
}

TEST_F(QuicGsoBatchWriterTest, WriteErrorWithoutDataBuffered) {
  TestQuicGsoBatchWriter writer(/*fd=*/-1);

  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 100));
  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 100));

  EXPECT_CALL(mock_syscalls_, Sendmsg(_, _, _))
      .WillOnce(Invoke([](int /*sockfd*/, const msghdr* msg, int /*flags*/) {
        EXPECT_EQ(200u, PacketLength(msg));
        errno = EPERM;
        return -1;
      }));
  WriteResult error_result = WritePacket(&writer, 150);
  ASSERT_EQ(WriteResult(WRITE_STATUS_ERROR, EPERM), error_result);

  ASSERT_EQ(3u, error_result.dropped_packets);
  ASSERT_EQ(0u, writer.batch_buffer().SizeInUse());
  ASSERT_EQ(0u, writer.buffered_writes().size());
}

TEST_F(QuicGsoBatchWriterTest, WriteErrorAfterDataBuffered) {
  TestQuicGsoBatchWriter writer(/*fd=*/-1);

  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 100));
  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 100));

  EXPECT_CALL(mock_syscalls_, Sendmsg(_, _, _))
      .WillOnce(Invoke([](int /*sockfd*/, const msghdr* msg, int /*flags*/) {
        EXPECT_EQ(250u, PacketLength(msg));
        errno = EPERM;
        return -1;
      }));
  WriteResult error_result = WritePacket(&writer, 50);
  ASSERT_EQ(WriteResult(WRITE_STATUS_ERROR, EPERM), error_result);

  ASSERT_EQ(3u, error_result.dropped_packets);
  ASSERT_EQ(0u, writer.batch_buffer().SizeInUse());
  ASSERT_EQ(0u, writer.buffered_writes().size());
}

TEST_F(QuicGsoBatchWriterTest, FlushError) {
  TestQuicGsoBatchWriter writer(/*fd=*/-1);

  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 100));
  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 0), WritePacket(&writer, 100));

  EXPECT_CALL(mock_syscalls_, Sendmsg(_, _, _))
      .WillOnce(Invoke([](int /*sockfd*/, const msghdr* msg, int /*flags*/) {
        EXPECT_EQ(200u, PacketLength(msg));
        errno = EINVAL;
        return -1;
      }));
  WriteResult error_result = writer.Flush();
  ASSERT_EQ(WriteResult(WRITE_STATUS_ERROR, EINVAL), error_result);

  ASSERT_EQ(2u, error_result.dropped_packets);
  ASSERT_EQ(0u, writer.batch_buffer().SizeInUse());
  ASSERT_EQ(0u, writer.buffered_writes().size());
}

TEST_F(QuicGsoBatchWriterTest, ReleaseTimeNullOptions) {
  auto writer = TestQuicGsoBatchWriter::NewInstanceWithReleaseTimeSupport();
  EXPECT_EQ(0u, writer->GetReleaseTime(nullptr).actual_release_time);
}

TEST_F(QuicGsoBatchWriterTest, ReleaseTime) {
  const WriteResult write_buffered(WRITE_STATUS_OK, 0);

  auto writer = TestQuicGsoBatchWriter::NewInstanceWithReleaseTimeSupport();

  TestPerPacketOptions options;
  EXPECT_TRUE(options.release_time_delay.IsZero());
  EXPECT_FALSE(options.allow_burst);
  EXPECT_EQ(MillisToNanos(1),
            writer->GetReleaseTime(&options).actual_release_time);

  // The 1st packet has no delay.
  WriteResult result = WritePacketWithOptions(writer.get(), &options);
  ASSERT_EQ(write_buffered, result);
  EXPECT_EQ(MillisToNanos(1), writer->buffered_writes().back().release_time);
  EXPECT_EQ(result.send_time_offset, QuicTime::Delta::Zero());

  // The 2nd packet has some delay, but allows burst.
  options.release_time_delay = QuicTime::Delta::FromMilliseconds(3);
  options.allow_burst = true;
  result = WritePacketWithOptions(writer.get(), &options);
  ASSERT_EQ(write_buffered, result);
  EXPECT_EQ(MillisToNanos(1), writer->buffered_writes().back().release_time);
  EXPECT_EQ(result.send_time_offset, QuicTime::Delta::FromMilliseconds(-3));

  // The 3rd packet has more delay and does not allow burst.
  // The first 2 packets are flushed due to different release time.
  EXPECT_CALL(mock_syscalls_, Sendmsg(_, _, _))
      .WillOnce(Invoke([](int /*sockfd*/, const msghdr* msg, int /*flags*/) {
        EXPECT_EQ(2700u, PacketLength(msg));
        errno = 0;
        return 0;
      }));
  options.release_time_delay = QuicTime::Delta::FromMilliseconds(5);
  options.allow_burst = false;
  result = WritePacketWithOptions(writer.get(), &options);
  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 2700), result);
  EXPECT_EQ(MillisToNanos(6), writer->buffered_writes().back().release_time);
  EXPECT_EQ(result.send_time_offset, QuicTime::Delta::Zero());

  // The 4th packet has same delay, but allows burst.
  options.allow_burst = true;
  result = WritePacketWithOptions(writer.get(), &options);
  ASSERT_EQ(write_buffered, result);
  EXPECT_EQ(MillisToNanos(6), writer->buffered_writes().back().release_time);
  EXPECT_EQ(result.send_time_offset, QuicTime::Delta::Zero());

  // The 5th packet has same delay, allows burst, but is shorter.
  // Packets 3,4 and 5 are flushed.
  EXPECT_CALL(mock_syscalls_, Sendmsg(_, _, _))
      .WillOnce(Invoke([](int /*sockfd*/, const msghdr* msg, int /*flags*/) {
        EXPECT_EQ(3000u, PacketLength(msg));
        errno = 0;
        return 0;
      }));
  options.allow_burst = true;
  EXPECT_EQ(MillisToNanos(6),
            writer->GetReleaseTime(&options).actual_release_time);
  ASSERT_EQ(WriteResult(WRITE_STATUS_OK, 3000),
            writer->WritePacket(&packet_buffer_[0], 300, self_address_,
                                peer_address_, &options));
  EXPECT_TRUE(writer->buffered_writes().empty());

  // Pretend 1ms has elapsed and the 6th packet has 1ms less delay. In other
  // words, the release time should still be the same as packets 3-5.
  writer->ForceReleaseTimeMs(2);
  options.release_time_delay = QuicTime::Delta::FromMilliseconds(4);
  result = WritePacketWithOptions(writer.get(), &options);
  ASSERT_EQ(write_buffered, result);
  EXPECT_EQ(MillisToNanos(6), writer->buffered_writes().back().release_time);
  EXPECT_EQ(result.send_time_offset, QuicTime::Delta::Zero());
}

}  // namespace
}  // namespace test
}  // namespace quic
