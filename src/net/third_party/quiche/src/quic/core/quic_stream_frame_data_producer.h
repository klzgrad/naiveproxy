// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_STREAM_FRAME_DATA_PRODUCER_H_
#define QUICHE_QUIC_CORE_QUIC_STREAM_FRAME_DATA_PRODUCER_H_

#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

class QuicDataWriter;

// Pure virtual class to retrieve stream data.
class QUIC_EXPORT_PRIVATE QuicStreamFrameDataProducer {
 public:
  virtual ~QuicStreamFrameDataProducer() {}

  // Let |writer| write |data_length| data with |offset| of stream |id|. The
  // write fails when either stream is closed or corresponding data is failed to
  // be retrieved. This method allows writing a single stream frame from data
  // that spans multiple buffers.
  virtual WriteStreamDataResult WriteStreamData(QuicStreamId id,
                                                QuicStreamOffset offset,
                                                QuicByteCount data_length,
                                                QuicDataWriter* writer) = 0;

  // Writes the data for a CRYPTO frame to |writer| for a frame at encryption
  // level |level| starting at offset |offset| for |data_length| bytes. Returns
  // whether writing the data was successful.
  virtual bool WriteCryptoData(EncryptionLevel level,
                               QuicStreamOffset offset,
                               QuicByteCount data_length,
                               QuicDataWriter* writer) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_FRAME_DATA_PRODUCER_H_
