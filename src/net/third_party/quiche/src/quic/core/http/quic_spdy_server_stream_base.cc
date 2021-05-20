// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/http/quic_spdy_server_stream_base.h"

#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_session.h"
#include "quic/platform/api/quic_logging.h"

namespace quic {

QuicSpdyServerStreamBase::QuicSpdyServerStreamBase(QuicStreamId id,
                                                   QuicSpdySession* session,
                                                   StreamType type)
    : QuicSpdyStream(id, session, type) {}

QuicSpdyServerStreamBase::QuicSpdyServerStreamBase(PendingStream* pending,
                                                   QuicSpdySession* session,
                                                   StreamType type)
    : QuicSpdyStream(pending, session, type) {}

void QuicSpdyServerStreamBase::CloseWriteSide() {
  if (!fin_received() && !rst_received() && sequencer()->ignore_read_data() &&
      !rst_sent()) {
    // Early cancel the stream if it has stopped reading before receiving FIN
    // or RST.
    QUICHE_DCHECK(fin_sent() || !session()->connection()->connected());
    // Tell the peer to stop sending further data.
    QUIC_DVLOG(1) << " Server: Send QUIC_STREAM_NO_ERROR on stream " << id();
    MaybeSendStopSending(QUIC_STREAM_NO_ERROR);
  }

  QuicSpdyStream::CloseWriteSide();
}

void QuicSpdyServerStreamBase::StopReading() {
  if (!fin_received() && !rst_received() && write_side_closed() &&
      !rst_sent()) {
    QUICHE_DCHECK(fin_sent());
    // Tell the peer to stop sending further data.
    QUIC_DVLOG(1) << " Server: Send QUIC_STREAM_NO_ERROR on stream " << id();
    MaybeSendStopSending(QUIC_STREAM_NO_ERROR);
  }
  QuicSpdyStream::StopReading();
}

}  // namespace quic
