// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_receive_stream.h"

#include "net/third_party/quiche/src/quic/core/quic_session.h"

namespace quic {
QpackReceiveStream::QpackReceiveStream(PendingStream* pending)
    : QuicStream(pending, READ_UNIDIRECTIONAL, /*is_static=*/true) {}

void QpackReceiveStream::OnStreamReset(const QuicRstStreamFrame& /*frame*/) {
  // TODO(renjietang) Change the error code to H/3 specific
  // HTTP_CLOSED_CRITICAL_STREAM.
  session()->connection()->CloseConnection(
      QUIC_INVALID_STREAM_ID, "Attempt to reset Qpack receive stream",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

}  // namespace quic
