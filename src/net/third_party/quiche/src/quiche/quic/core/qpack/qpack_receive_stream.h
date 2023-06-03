// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_RECEIVE_STREAM_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_RECEIVE_STREAM_H_

#include "quiche/quic/core/qpack/qpack_stream_receiver.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QuicSession;

// QPACK 4.2.1 Encoder and Decoder Streams.
// The QPACK receive stream is peer initiated and is read only.
class QUIC_EXPORT_PRIVATE QpackReceiveStream : public QuicStream {
 public:
  // Construct receive stream from pending stream, the |pending| object needs
  // to be deleted after the construction.
  QpackReceiveStream(PendingStream* pending, QuicSession* session,
                     QpackStreamReceiver* receiver);
  QpackReceiveStream(const QpackReceiveStream&) = delete;
  QpackReceiveStream& operator=(const QpackReceiveStream&) = delete;
  ~QpackReceiveStream() override = default;

  // Overriding QuicStream::OnStreamReset to make sure QPACK stream is never
  // closed before connection.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;

  // Implementation of QuicStream.
  void OnDataAvailable() override;

 private:
  QpackStreamReceiver* receiver_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_RECEIVE_STREAM_H_
