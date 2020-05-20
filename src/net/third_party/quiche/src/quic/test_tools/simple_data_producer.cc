// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/simple_data_producer.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace test {

SimpleDataProducer::SimpleDataProducer() {}

SimpleDataProducer::~SimpleDataProducer() {}

void SimpleDataProducer::SaveStreamData(QuicStreamId id,
                                        const struct iovec* iov,
                                        int iov_count,
                                        size_t iov_offset,
                                        QuicByteCount data_length) {
  if (data_length == 0) {
    return;
  }
  if (!QuicContainsKey(send_buffer_map_, id)) {
    send_buffer_map_[id] = std::make_unique<QuicStreamSendBuffer>(&allocator_);
  }
  send_buffer_map_[id]->SaveStreamData(iov, iov_count, iov_offset, data_length);
}

void SimpleDataProducer::SaveCryptoData(EncryptionLevel level,
                                        QuicStreamOffset offset,
                                        quiche::QuicheStringPiece data) {
  auto key = std::make_pair(level, offset);
  crypto_buffer_map_[key] = data;
}

WriteStreamDataResult SimpleDataProducer::WriteStreamData(
    QuicStreamId id,
    QuicStreamOffset offset,
    QuicByteCount data_length,
    QuicDataWriter* writer) {
  auto iter = send_buffer_map_.find(id);
  if (iter == send_buffer_map_.end()) {
    return STREAM_MISSING;
  }
  if (iter->second->WriteStreamData(offset, data_length, writer)) {
    return WRITE_SUCCESS;
  }
  return WRITE_FAILED;
}

bool SimpleDataProducer::WriteCryptoData(EncryptionLevel level,
                                         QuicStreamOffset offset,
                                         QuicByteCount data_length,
                                         QuicDataWriter* writer) {
  auto it = crypto_buffer_map_.find(std::make_pair(level, offset));
  if (it == crypto_buffer_map_.end() || it->second.length() < data_length) {
    return false;
  }
  return writer->WriteStringPiece(
      quiche::QuicheStringPiece(it->second.data(), data_length));
}

}  // namespace test

}  // namespace quic
