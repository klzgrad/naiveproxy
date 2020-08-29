// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_stream.h"

#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_constants.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_session_base.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

ABSL_FLAG(int, qbone_stream_ttl_secs, 3, "The QBONE Stream TTL in seconds.");

namespace quic {

QboneWriteOnlyStream::QboneWriteOnlyStream(QuicStreamId id,
                                           QuicSession* session)
    : QuicStream(id, session, /*is_static=*/false, WRITE_UNIDIRECTIONAL) {
  // QBONE uses a LIFO queue to try to always make progress. An individual
  // packet may persist for upto to qbone_stream_ttl_secs seconds in memory.
  MaybeSetTtl(
      QuicTime::Delta::FromSeconds(GetQuicFlag(FLAGS_qbone_stream_ttl_secs)));
}

void QboneWriteOnlyStream::WritePacketToQuicStream(
    quiche::QuicheStringPiece packet) {
  // Streams are one way and ephemeral. This function should only be
  // called once.
  WriteOrBufferData(packet, /* fin= */ true, nullptr);
}

QboneReadOnlyStream::QboneReadOnlyStream(QuicStreamId id,
                                         QboneSessionBase* session)
    : QuicStream(id,
                 session,
                 /*is_static=*/false,
                 READ_UNIDIRECTIONAL),
      session_(session) {
  // QBONE uses a LIFO queue to try to always make progress. An individual
  // packet may persist for upto to qbone_stream_ttl_secs seconds in memory.
  MaybeSetTtl(
      QuicTime::Delta::FromSeconds(GetQuicFlag(FLAGS_qbone_stream_ttl_secs)));
}

void QboneReadOnlyStream::OnDataAvailable() {
  // Read in data and buffer it, attempt to frame to see if there's a packet.
  sequencer()->Read(&buffer_);
  if (sequencer()->IsClosed()) {
    session_->ProcessPacketFromPeer(buffer_);
    OnFinRead();
    return;
  }
  if (buffer_.size() > QboneConstants::kMaxQbonePacketBytes) {
    if (!rst_sent()) {
      Reset(QUIC_BAD_APPLICATION_PAYLOAD);
    }
    StopReading();
  }
}

}  // namespace quic
