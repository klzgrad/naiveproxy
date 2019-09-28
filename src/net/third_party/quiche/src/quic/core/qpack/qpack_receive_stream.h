// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_RECEIVE_STREAM_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_RECEIVE_STREAM_H_

#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class QuicSession;

// QPACK 4.2.1 Encoder and Decoder Streams.
// The QPACK receive stream is peer initiated and is read only.
class QUIC_EXPORT_PRIVATE QpackReceiveStream : public QuicStream {
 public:
  // Construct receive stream from pending stream, the |pending| object needs
  // to be deleted after the construction.
  explicit QpackReceiveStream(PendingStream* pending);
  QpackReceiveStream(const QpackReceiveStream&) = delete;
  QpackReceiveStream& operator=(const QpackReceiveStream&) = delete;
  ~QpackReceiveStream() override = default;

  // Overriding QuicStream::OnStreamReset to make sure QPACK stream is never
  // closed before connection.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;

  // Implementation of QuicStream. Unimplemented yet.
  // TODO(bnc): Feed data to QPACK.
  void OnDataAvailable() override {}
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_RECEIVE_STREAM_H_
