// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/simple_data_producer.h"

#include "net/quic/platform/api/quic_map_util.h"

namespace net {

namespace test {

SimpleDataProducer::SimpleDataProducer() {}
SimpleDataProducer::~SimpleDataProducer() {}

void SimpleDataProducer::SaveStreamData(QuicStreamId id,
                                        QuicIOVector iov,
                                        size_t iov_offset,
                                        QuicStreamOffset offset,
                                        QuicByteCount data_length) {
  if (!QuicContainsKey(send_buffer_map_, id)) {
    send_buffer_map_[id].reset(new QuicStreamSendBuffer(&allocator_));
  }
  send_buffer_map_[id]->SaveStreamData(iov, iov_offset, data_length);
}

bool SimpleDataProducer::WriteStreamData(QuicStreamId id,
                                         QuicStreamOffset offset,
                                         QuicByteCount data_length,
                                         QuicDataWriter* writer) {
  return send_buffer_map_[id]->WriteStreamData(offset, data_length, writer);
}

}  // namespace test

}  // namespace net
