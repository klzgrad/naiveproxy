// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/batch_writer/quic_sendmmsg_batch_writer.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/impl/batch_writer/quic_batch_writer_test.h"

namespace quic {
namespace test {
namespace {

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

INSTANTIATE_TEST_CASE_P(
    QuicSendmmsgBatchWriterTest,
    QuicUdpBatchWriterIOTest,
    testing::ValuesIn(MakeQuicBatchWriterTestParams<
                      QuicSendmmsgBatchWriterIOTestDelegate>()));

}  // namespace
}  // namespace test
}  // namespace quic
