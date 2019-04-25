// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/batch_writer/quic_batch_writer_test.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/impl/batch_writer/quic_gso_batch_writer.h"
#include "net/third_party/quic/platform/impl/batch_writer/quic_sendmmsg_batch_writer.h"

namespace quic {
namespace test {
namespace {

class QuicGsoBatchWriterIOTestDelegate
    : public QuicUdpBatchWriterIOTestDelegate {
 public:
  bool ShouldSkip(const QuicUdpBatchWriterIOTestParams& params) override {
    const QuicSocketAddress address =
        params.address_family == AF_INET
            ? QuicSocketAddress(QuicIpAddress::Loopback4(), 0)
            : QuicSocketAddress(QuicIpAddress::Loopback6(), 0);

    bool overflow_supported = false;
    int fd = QuicSocketUtils::CreateUDPSocket(
        address, /*receive_buffer_size=*/kDefaultSocketReceiveBuffer,
        /*send_buffer_size=*/kDefaultSocketReceiveBuffer, &overflow_supported);
    if (fd < 0) {
      QUIC_LOG(ERROR) << "CreateSocket() failed: " << strerror(errno);
      return false;  // Let the test fail rather than skip it.
    }
    const bool gso_not_supported =
        QuicLinuxSocketUtils::GetUDPSegmentSize(fd) < 0;
    close(fd);

    if (gso_not_supported) {
      QUIC_LOG(WARNING) << "Test skipped since GSO is not supported.";
      return true;
    }

    QUIC_LOG(WARNING) << "OK: GSO is supported.";
    return false;
  }

  void ResetWriter(int fd) override {
    writer_ = QuicMakeUnique<QuicGsoBatchWriter>(
        QuicMakeUnique<QuicBatchWriterBuffer>(), fd);
  }

  QuicUdpBatchWriter* GetWriter() override { return writer_.get(); }

 private:
  std::unique_ptr<QuicGsoBatchWriter> writer_;
};

INSTANTIATE_TEST_SUITE_P(
    QuicGsoBatchWriterTest,
    QuicUdpBatchWriterIOTest,
    testing::ValuesIn(
        MakeQuicBatchWriterTestParams<QuicGsoBatchWriterIOTestDelegate>()));

class QuicSendmmsgBatchWriterIOTestDelegate
    : public QuicUdpBatchWriterIOTestDelegate {
 public:
  void ResetWriter(int fd) override {
    writer_ = QuicMakeUnique<QuicSendmmsgBatchWriter>(
        QuicMakeUnique<QuicBatchWriterBuffer>(), fd);
  }

  QuicUdpBatchWriter* GetWriter() override { return writer_.get(); }

 private:
  std::unique_ptr<QuicSendmmsgBatchWriter> writer_;
};

INSTANTIATE_TEST_SUITE_P(
    QuicSendmmsgBatchWriterTest,
    QuicUdpBatchWriterIOTest,
    testing::ValuesIn(MakeQuicBatchWriterTestParams<
                      QuicSendmmsgBatchWriterIOTestDelegate>()));

}  // namespace
}  // namespace test
}  // namespace quic
