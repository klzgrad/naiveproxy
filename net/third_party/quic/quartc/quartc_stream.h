// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_STREAM_H_
#define NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_STREAM_H_

#include "net/third_party/quic/core/quic_session.h"
#include "net/third_party/quic/core/quic_stream.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/quartc/quartc_stream_interface.h"

namespace net {

// Implements a QuartcStreamInterface using a QuicStream.
class QUIC_EXPORT_PRIVATE QuartcStream : public QuicStream,
                                         public QuartcStreamInterface {
 public:
  QuartcStream(QuicStreamId id, QuicSession* session);

  ~QuartcStream() override;

  // QuicStream overrides.
  void OnDataAvailable() override;

  void OnClose() override;

  void OnStreamDataConsumed(size_t bytes_consumed) override;

  void OnDataBuffered(
      QuicStreamOffset offset,
      QuicByteCount data_length,
      const QuicReferenceCountedPointer<QuicAckListenerInterface>& ack_listener)
      override;

  // QuartcStreamInterface overrides.
  uint32_t stream_id() override;

  uint64_t bytes_buffered() override;

  bool fin_sent() override;

  int stream_error() override;

  void Write(QuicMemSliceSpan data, const WriteParameters& param) override;

  void FinishWriting() override;

  void FinishReading() override;

  void Close() override;

  void SetDelegate(QuartcStreamInterface::Delegate* delegate) override;

 private:
  QuartcStreamInterface::Delegate* delegate_ = nullptr;
};

}  // namespace net

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_STREAM_H_
