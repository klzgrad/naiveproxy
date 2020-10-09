// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_SEND_STREAM_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_SEND_STREAM_H_

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_stream_sender_delegate.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QuicSession;

// QPACK 4.2.1 Encoder and Decoder Streams.
// The QPACK send stream is self initiated and is write only.
class QUIC_EXPORT_PRIVATE QpackSendStream : public QuicStream,
                                            public QpackStreamSenderDelegate {
 public:
  // |session| can't be nullptr, and the ownership is not passed. |session| owns
  // this stream.
  QpackSendStream(QuicStreamId id,
                  QuicSession* session,
                  uint64_t http3_stream_type);
  QpackSendStream(const QpackSendStream&) = delete;
  QpackSendStream& operator=(const QpackSendStream&) = delete;
  ~QpackSendStream() override = default;

  // Overriding QuicStream::OnStopSending() to make sure QPACK stream is never
  // closed before connection.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;
  bool OnStopSending(uint16_t code) override;

  // The send QPACK stream is write unidirectional, so this method
  // should never be called.
  void OnDataAvailable() override { QUIC_NOTREACHED(); }

  // Writes the instructions to peer. The stream type will be sent
  // before the first instruction so that the peer can open an qpack stream.
  void WriteStreamData(quiche::QuicheStringPiece data) override;

  // TODO(b/112770235): Remove this method once QuicStreamIdManager supports
  // creating HTTP/3 unidirectional streams dynamically.
  void MaybeSendStreamType();

 private:
  const uint64_t http3_stream_type_;
  bool stream_type_sent_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_SEND_STREAM_H_
