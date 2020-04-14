// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_STREAM_SENDER_DELEGATE_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_STREAM_SENDER_DELEGATE_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// This interface writes encoder/decoder data to peer.
class QUIC_EXPORT_PRIVATE QpackStreamSenderDelegate {
 public:
  virtual ~QpackStreamSenderDelegate() = default;

  // Write data on the unidirectional stream.
  virtual void WriteStreamData(quiche::QuicheStringPiece data) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_STREAM_SENDER_DELEGATE_H_
