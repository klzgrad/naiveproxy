// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_SIMPLE_DATA_PRODUCER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_SIMPLE_DATA_PRODUCER_H_

#include "net/third_party/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quic/core/quic_stream_frame_data_producer.h"
#include "net/third_party/quic/core/quic_stream_send_buffer.h"
#include "net/third_party/quic/platform/api/quic_containers.h"

namespace quic {

namespace test {

// A simple data producer which copies stream data into a map from stream
// id to send buffer.
class SimpleDataProducer : public QuicStreamFrameDataProducer {
 public:
  SimpleDataProducer();
  ~SimpleDataProducer() override;

  void SaveStreamData(QuicStreamId id,
                      const struct iovec* iov,
                      int iov_count,
                      size_t iov_offset,
                      QuicStreamOffset offset,
                      QuicByteCount data_length);

  // QuicStreamFrameDataProducer
  bool WriteStreamData(QuicStreamId id,
                       QuicStreamOffset offset,
                       QuicByteCount data_length,
                       QuicDataWriter* writer) override;

 private:
  using SendBufferMap =
      QuicUnorderedMap<QuicStreamId, std::unique_ptr<QuicStreamSendBuffer>>;

  SimpleBufferAllocator allocator_;

  SendBufferMap send_buffer_map_;
};

}  // namespace test

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_SIMPLE_DATA_PRODUCER_H_
