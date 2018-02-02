// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/simple_data_producer.h"

#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_map_util.h"
#include "net/quic/platform/api/quic_ptr_util.h"

namespace net {

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
    send_buffer_map_[id] = QuicMakeUnique<QuicStreamSendBuffer>(
        &allocator_,
        FLAGS_quic_reloadable_flag_quic_allow_multiple_acks_for_data2);
  }
  send_buffer_map_[id]->SaveStreamData(iov, iov_count, iov_offset, data_length);
}

bool SimpleDataProducer::WriteStreamData(QuicStreamId id,
                                         QuicStreamOffset offset,
                                         QuicByteCount data_length,
                                         QuicDataWriter* writer) {
  return send_buffer_map_[id]->WriteStreamData(offset, data_length, writer);
}

void SimpleDataProducer::OnStreamFrameAcked(
    const QuicStreamFrame& frame,
    QuicTime::Delta /*ack_delay_time*/) {
  OnStreamFrameDiscarded(frame);
}

void SimpleDataProducer::OnStreamFrameDiscarded(const QuicStreamFrame& frame) {
  if (!QuicContainsKey(send_buffer_map_, frame.stream_id)) {
    return;
  }
  QuicByteCount newly_acked_length = 0;
  send_buffer_map_[frame.stream_id]->OnStreamDataAcked(
      frame.offset, frame.data_length, &newly_acked_length);
}

}  // namespace test

}  // namespace net
