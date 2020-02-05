// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_MOCK_QBONE_CLIENT_H_
#define QUICHE_QUIC_QBONE_MOCK_QBONE_CLIENT_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_client_interface.h"

namespace quic {

class MockQboneClient : public QboneClientInterface {
 public:
  MOCK_METHOD1(ProcessPacketFromNetwork, void(QuicStringPiece packet));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_MOCK_QBONE_CLIENT_H_
