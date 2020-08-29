// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_receive_stream.h"

#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
QpackReceiveStream::QpackReceiveStream(PendingStream* pending,
                                       QpackStreamReceiver* receiver)
    : QuicStream(pending, READ_UNIDIRECTIONAL, /*is_static=*/true),
      receiver_(receiver) {}

void QpackReceiveStream::OnStreamReset(const QuicRstStreamFrame& /*frame*/) {
  stream_delegate()->OnStreamError(
      QUIC_HTTP_CLOSED_CRITICAL_STREAM,
      "RESET_STREAM received for QPACK receive stream");
}

void QpackReceiveStream::OnDataAvailable() {
  iovec iov;
  while (!reading_stopped() && sequencer()->GetReadableRegion(&iov)) {
    DCHECK(!sequencer()->IsClosed());

    receiver_->Decode(quiche::QuicheStringPiece(
        reinterpret_cast<const char*>(iov.iov_base), iov.iov_len));
    sequencer()->MarkConsumed(iov.iov_len);
  }
}

}  // namespace quic
