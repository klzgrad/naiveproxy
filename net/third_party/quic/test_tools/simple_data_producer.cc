// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/test_tools/simple_data_producer.h"

#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_map_util.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"

namespace quic {

namespace test {

SimpleDataProducer::SimpleDataProducer() {}
SimpleDataProducer::~SimpleDataProducer() {}

void SimpleDataProducer::SaveStreamData(QuicStreamId id,
                                        const struct iovec* iov,
                                        int iov_count,
                                        size_t iov_offset,
                                        QuicStreamOffset offset,
                                        QuicByteCount data_length) {
  if (data_length == 0) {
    return;
  }
  if (!QuicContainsKey(send_buffer_map_, id)) {
    send_buffer_map_[id] = QuicMakeUnique<QuicStreamSendBuffer>(&allocator_);
  }
  send_buffer_map_[id]->SaveStreamData(iov, iov_count, iov_offset, data_length);
}

bool SimpleDataProducer::WriteStreamData(QuicStreamId id,
                                         QuicStreamOffset offset,
                                         QuicByteCount data_length,
                                         QuicDataWriter* writer) {
  return send_buffer_map_[id]->WriteStreamData(offset, data_length, writer);
}

}  // namespace test

}  // namespace quic
