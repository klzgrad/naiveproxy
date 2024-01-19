// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/simple_data_producer.h"

#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

namespace test {

SimpleDataProducer::SimpleDataProducer() {}

SimpleDataProducer::~SimpleDataProducer() {}

void SimpleDataProducer::SaveStreamData(QuicStreamId id,
                                        absl::string_view data) {
  if (data.empty()) {
    return;
  }
  if (!send_buffer_map_.contains(id)) {
    send_buffer_map_[id] = std::make_unique<QuicStreamSendBuffer>(&allocator_);
  }
  send_buffer_map_[id]->SaveStreamData(data);
}

void SimpleDataProducer::SaveCryptoData(EncryptionLevel level,
                                        QuicStreamOffset offset,
                                        absl::string_view data) {
  auto key = std::make_pair(level, offset);
  crypto_buffer_map_[key] = std::string(data);
}

WriteStreamDataResult SimpleDataProducer::WriteStreamData(
    QuicStreamId id, QuicStreamOffset offset, QuicByteCount data_length,
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
      absl::string_view(it->second.data(), data_length));
}

}  // namespace test

}  // namespace quic
