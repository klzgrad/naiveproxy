// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_CLIENT_INTERFACE_H_
#define QUICHE_QUIC_QBONE_QBONE_CLIENT_INTERFACE_H_

#include <cstdint>

#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// An interface that includes methods to interact with a QBONE client.
class QboneClientInterface {
 public:
  virtual ~QboneClientInterface() {}
  // Accepts a given packet from the network and sends the packet down to the
  // QBONE connection.
  virtual void ProcessPacketFromNetwork(quiche::QuicheStringPiece packet) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_CLIENT_INTERFACE_H_
