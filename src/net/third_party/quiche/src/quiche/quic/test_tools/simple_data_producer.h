// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SIMPLE_DATA_PRODUCER_H_
#define QUICHE_QUIC_TEST_TOOLS_SIMPLE_DATA_PRODUCER_H_

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_stream_frame_data_producer.h"
#include "quiche/quic/core/quic_stream_send_buffer.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace quic {

namespace test {

// A simple data producer which copies stream data into a map from stream
// id to send buffer.
class SimpleDataProducer : public QuicStreamFrameDataProducer {
 public:
  SimpleDataProducer();
  ~SimpleDataProducer() override;

  // Saves `data` to be provided when WriteStreamData() is called. Multiple
  // calls to SaveStreamData() for the same stream ID append to the buffer for
  // that stream.
  void SaveStreamData(QuicStreamId id, absl::string_view data);

  void SaveCryptoData(EncryptionLevel level, QuicStreamOffset offset,
                      absl::string_view data);

  // QuicStreamFrameDataProducer
  WriteStreamDataResult WriteStreamData(QuicStreamId id,
                                        QuicStreamOffset offset,
                                        QuicByteCount data_length,
                                        QuicDataWriter* writer) override;
  bool WriteCryptoData(EncryptionLevel level, QuicStreamOffset offset,
                       QuicByteCount data_length,
                       QuicDataWriter* writer) override;

 private:
  using SendBufferMap =
      absl::flat_hash_map<QuicStreamId, std::unique_ptr<QuicStreamSendBuffer>>;

  using CryptoBufferMap =
      absl::flat_hash_map<std::pair<EncryptionLevel, QuicStreamOffset>,
                          std::string>;

  quiche::SimpleBufferAllocator allocator_;

  SendBufferMap send_buffer_map_;

  // |crypto_buffer_map_| stores data provided by SaveCryptoData to later write
  // in WriteCryptoData. The level and data passed into SaveCryptoData are used
  // as the key to identify the data when WriteCryptoData is called.
  // WriteCryptoData will only succeed if there is data in the map for the
  // provided level and offset, and the data in the map matches the data_length
  // passed into WriteCryptoData.
  //
  // Unlike SaveStreamData/WriteStreamData which uses a map of
  // QuicStreamSendBuffers (for each stream ID), this map provides data for
  // specific offsets. Using a QuicStreamSendBuffer requires that all data
  // before an offset exist, whereas this allows providing data that exists at
  // arbitrary offsets for testing.
  CryptoBufferMap crypto_buffer_map_;
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_SIMPLE_DATA_PRODUCER_H_
