// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/batch_writer/quic_gso_batch_writer.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class TestQuicGsoBatchWriter : public QuicGsoBatchWriter {
 public:
  using QuicGsoBatchWriter::batch_buffer;
  using QuicGsoBatchWriter::CanBatch;
  using QuicGsoBatchWriter::CanBatchResult;
  using QuicGsoBatchWriter::MaxSegments;
  using QuicGsoBatchWriter::QuicGsoBatchWriter;
};

// TestBufferedWrite is a copy-constructible BufferedWrite.
struct TestBufferedWrite : public BufferedWrite {
  using BufferedWrite::BufferedWrite;
  TestBufferedWrite(const TestBufferedWrite& other)
      : BufferedWrite(other.buffer,
                      other.buf_len,
                      other.self_address,
                      other.peer_address,
                      other.options ? other.options->Clone()
                                    : std::unique_ptr<PerPacketOptions>()) {}
};

// Pointed to by all instances of |BatchCriteriaTestData|. Content not used.
static char unused_packet_buffer[kMaxPacketSize];

struct BatchCriteriaTestData {
  BatchCriteriaTestData(size_t buf_len,
                        const QuicIpAddress& self_address,
                        const QuicSocketAddress& peer_address,
                        bool can_batch,
                        bool must_flush)
      : buffered_write(unused_packet_buffer,
                       buf_len,
                       self_address,
                       peer_address),
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
  // buf_len   self_addr   peer_addr         can_batch       must_flush
    {1350,     self_addr,  peer_addr,        true,           false},
    {1350,     self_addr,  peer_addr,        true,           false},
    {1350,     self_addr,  peer_addr,        true,           false},
    {39,       self_addr,  peer_addr,        true,           true},
    {39,       self_addr,  peer_addr,        false,          true},
    {1350,     self_addr,  peer_addr,        false,          true},
      // clang-format on
  };
  return test_data_table;
}

std::vector<BatchCriteriaTestData> BatchCriteriaTestData_SizeIncrease() {
  const QuicIpAddress self_addr;
  const QuicSocketAddress peer_addr;
  std::vector<BatchCriteriaTestData> test_data_table = {
      // clang-format off
  // buf_len   self_addr   peer_addr         can_batch       must_flush
    {1350,     self_addr,  peer_addr,        true,           false},
    {1350,     self_addr,  peer_addr,        true,           false},
    {1350,     self_addr,  peer_addr,        true,           false},
    {1351,     self_addr,  peer_addr,        false,          true},
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
  // buf_len   self_addr   peer_addr         can_batch       must_flush
    {1350,     self_addr1, peer_addr1,       true,           false},
    {1350,     self_addr1, peer_addr1,       true,           false},
    {1350,     self_addr1, peer_addr1,       true,           false},
    {1350,     self_addr2, peer_addr1,       false,          true},
    {1350,     self_addr1, peer_addr2,       false,          true},
    {1350,     self_addr1, peer_addr3,       false,          true},
    {1350,     self_addr1, peer_addr4,       false,          true},
    {1350,     self_addr1, peer_addr4,       false,          true},
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
    test_data_table.push_back(
        {gso_size, self_addr, peer_addr, true, is_last_in_batch});
  }
  test_data_table.push_back({gso_size, self_addr, peer_addr, false, true});
  return test_data_table;
}

TEST(QuicGsoBatchWriterTest, BatchCriteria) {
  std::unique_ptr<TestQuicGsoBatchWriter> writer;

  std::vector<std::vector<BatchCriteriaTestData>> test_data_tables;
  test_data_tables.emplace_back(BatchCriteriaTestData_SizeDecrease());
  test_data_tables.emplace_back(BatchCriteriaTestData_SizeIncrease());
  test_data_tables.emplace_back(BatchCriteriaTestData_AddressChange());
  test_data_tables.emplace_back(BatchCriteriaTestData_MaxSegments(1));
  test_data_tables.emplace_back(BatchCriteriaTestData_MaxSegments(2));
  test_data_tables.emplace_back(BatchCriteriaTestData_MaxSegments(1350));

  for (size_t i = 0; i < test_data_tables.size(); ++i) {
    writer = QuicMakeUnique<TestQuicGsoBatchWriter>(
        QuicMakeUnique<QuicBatchWriterBuffer>(), /*fd=*/-1);

    const auto& test_data_table = test_data_tables[i];
    for (size_t j = 0; j < test_data_table.size(); ++j) {
      const BatchCriteriaTestData& test_data = test_data_table[j];
      SCOPED_TRACE(testing::Message() << "i=" << i << ", j=" << j);
      TestQuicGsoBatchWriter::CanBatchResult result = writer->CanBatch(
          test_data.buffered_write.buffer, test_data.buffered_write.buf_len,
          test_data.buffered_write.self_address,
          test_data.buffered_write.peer_address,
          /*options=*/nullptr);

      ASSERT_EQ(test_data.can_batch, result.can_batch);
      ASSERT_EQ(test_data.must_flush, result.must_flush);

      if (result.can_batch) {
        ASSERT_TRUE(
            writer->batch_buffer()
                .PushBufferedWrite(test_data.buffered_write.buffer,
                                   test_data.buffered_write.buf_len,
                                   test_data.buffered_write.self_address,
                                   test_data.buffered_write.peer_address,
                                   /*options=*/nullptr)
                .succeeded);
      }
    }
  }
}

}  // namespace
}  // namespace test
}  // namespace quic
