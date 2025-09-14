// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/qpack_receive_stream.h"

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_session.h"

namespace quic {
QpackReceiveStream::QpackReceiveStream(PendingStream* pending,
                                       QuicSession* session,
                                       QpackStreamReceiver* receiver)
    : QuicStream(pending, session, /*is_static=*/true), receiver_(receiver) {}

void QpackReceiveStream::OnStreamReset(const QuicRstStreamFrame& /*frame*/) {
  stream_delegate()->OnStreamError(
      QUIC_HTTP_CLOSED_CRITICAL_STREAM,
      "RESET_STREAM received for QPACK receive stream");
}

void QpackReceiveStream::OnDataAvailable() {
  iovec iov;
  while (!reading_stopped() && sequencer()->GetReadableRegion(&iov)) {
    QUICHE_DCHECK(!sequencer()->IsClosed());

    receiver_->Decode(absl::string_view(
        reinterpret_cast<const char*>(iov.iov_base), iov.iov_len));
    sequencer()->MarkConsumed(iov.iov_len);
  }
}

}  // namespace quic
