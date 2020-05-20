// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_STREAM_H_
#define QUICHE_QUIC_QBONE_QBONE_STREAM_H_

#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QboneSessionBase;

// QboneWriteOnlyStream is responsible for sending data for a single
// packet to the other side.
// Note that the stream will be created HalfClosed (reads will be closed).
class QUIC_EXPORT_PRIVATE QboneWriteOnlyStream : public QuicStream {
 public:
  QboneWriteOnlyStream(QuicStreamId id, QuicSession* session);

  // QuicStream implementation. QBONE writers are ephemeral and don't
  // read any data.
  void OnDataAvailable() override {}

  // Write a network packet over the quic stream.
  void WritePacketToQuicStream(quiche::QuicheStringPiece packet);
};

// QboneReadOnlyStream will be used if we find an incoming stream that
// isn't fully contained. It will buffer the data when available and
// attempt to parse it as a packet to send to the network when a FIN
// is found.
// Note that the stream will be created HalfClosed (writes will be closed).
class QUIC_EXPORT_PRIVATE QboneReadOnlyStream : public QuicStream {
 public:
  QboneReadOnlyStream(QuicStreamId id, QboneSessionBase* session);

  ~QboneReadOnlyStream() override = default;

  // QuicStream overrides.
  // OnDataAvailable is called when there is data in the quic stream buffer.
  // This will copy the buffer locally and attempt to parse it to write out
  // packets to the network.
  void OnDataAvailable() override;

 private:
  std::string buffer_;
  QboneSessionBase* session_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_STREAM_H_
